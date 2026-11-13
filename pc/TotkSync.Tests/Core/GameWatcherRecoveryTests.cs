using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Game;
using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class GameWatcherRecoveryTests : IDisposable
{
    private readonly TempDir _temp = new();
    private readonly TransferGate _gate = new();
    private readonly FakeGameProcess _game = new();
    private readonly List<bool> _runningChanges = [];
    private int _newProgress;
    private string _saveDir = "";

    public void Dispose() => _temp.Dispose();

    private string StatePath => Path.Combine(_temp.Path, "state", "state.json");

    private async Task<SaveStatusCache> GuardedCacheWithGameRunningAtStartupAsync(string? lastSyncFingerprint)
    {
        var gameDir = Path.Combine(_temp.Path, "game");
        _saveDir = SaveTreeBuilder.BuildGameDirWithSave(gameDir, "A");
        if (lastSyncFingerprint == "current")
        {
            lastSyncFingerprint = Fingerprint.OfDirectory(_saveDir);
        }
        if (lastSyncFingerprint is not null)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(StatePath)!);
            File.WriteAllText(StatePath, StatusRig.LastSyncJson("switch->pc").Replace("sha256:abc", lastSyncFingerprint));
        }
        var session = new SettingsSession(new SettingsStore(Path.Combine(_temp.Path, "settings.json")), new LauncherSettings(GameDir: gameDir));
        _game.Running = true;
        var cache = new SaveStatusCache(session, new SaveStatusReader(new SyncStateStore(StatePath)), _game);
        await cache.RefreshAsync();
        Assert.IsType<SaveStatus.GameRunning>(cache.Current);
        _game.Running = false;
        return cache;
    }

    private GameWatcher NewWatcher(SaveStatusCache cache, PendingRecovery? recovery = null)
    {
        var watcher = new GameWatcher(_game, _gate, cache, new FakeTimeProvider(), null, recovery);
        watcher.RunningChanged += _runningChanges.Add;
        watcher.PcSaveHasNewProgress += () => _newProgress++;
        return watcher;
    }

    [Fact]
    public async Task Card_stuck_on_GameRunning_is_refreshed_by_the_first_tick_without_a_RunningChanged()
    {
        var cache = await GuardedCacheWithGameRunningAtStartupAsync("current");
        var watcher = NewWatcher(cache);

        await watcher.TickAsync(CancellationToken.None);

        Assert.IsType<SaveStatus.Found>(cache.Current);
        Assert.Empty(_runningChanges);
        Assert.Equal(0, _newProgress);
    }

    [Fact]
    public async Task Card_stuck_on_GameRunning_with_a_save_that_differs_from_last_sync_raises_new_progress()
    {
        var cache = await GuardedCacheWithGameRunningAtStartupAsync("sha256:something-else");
        var watcher = NewWatcher(cache);

        await watcher.TickAsync(CancellationToken.None);

        Assert.Equal(1, _newProgress);
    }

    [Fact]
    public async Task Card_stuck_on_GameRunning_when_never_synced_raises_no_new_progress()
    {
        var cache = await GuardedCacheWithGameRunningAtStartupAsync(null);
        var watcher = NewWatcher(cache);

        await watcher.TickAsync(CancellationToken.None);

        Assert.IsType<SaveStatus.Found>(cache.Current);
        Assert.Equal(0, _newProgress);
    }

    [Fact]
    public async Task Ticks_after_the_card_is_fixed_do_not_refresh_again_or_raise_events()
    {
        var cache = await GuardedCacheWithGameRunningAtStartupAsync("sha256:something-else");
        var watcher = NewWatcher(cache);
        await watcher.TickAsync(CancellationToken.None);

        await watcher.TickAsync(CancellationToken.None);
        await watcher.TickAsync(CancellationToken.None);

        Assert.Equal((1, 0), (_newProgress, _runningChanges.Count));
    }

    [Fact]
    public async Task Watcher_runs_pending_recovery_before_the_refresh_that_follows_it()
    {
        var cache = await GuardedCacheWithGameRunningAtStartupAsync("current");
        var recovery = new PendingRecovery(_game, _gate, () =>
        {
            File.WriteAllText(Path.Combine(_saveDir, "slot_00", "progress.sav"), "restored by recovery");
            return [];
        }, _ => { });
        var watcher = NewWatcher(cache, recovery);

        await watcher.TickAsync(CancellationToken.None);

        var found = Assert.IsType<SaveStatus.Found>(cache.Current);
        Assert.Equal((Fingerprint.OfDirectory(_saveDir), false), (found.Snapshot.Fingerprint, recovery.IsPending));
    }

    [Fact]
    public async Task Watcher_defers_pending_recovery_while_the_game_runs_and_runs_it_after_exit()
    {
        var cache = await GuardedCacheWithGameRunningAtStartupAsync("current");
        var calls = 0;
        var recovery = new PendingRecovery(_game, _gate, () => { calls++; return []; }, _ => { });
        var watcher = NewWatcher(cache, recovery);
        _game.Running = true;
        await watcher.TickAsync(CancellationToken.None);
        var callsWhileRunning = calls;

        _game.Running = false;
        await watcher.TickAsync(CancellationToken.None);

        Assert.Equal((0, 1), (callsWhileRunning, calls));
    }
}
