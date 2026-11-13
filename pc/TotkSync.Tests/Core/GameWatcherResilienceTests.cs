using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Game;
using TotkSync.Core.Saves;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class GameWatcherResilienceTests : IDisposable
{
    private readonly TempDir _temp = new();
    private readonly TransferGate _gate = new();
    private readonly List<bool> _runningChanges = [];
    private int _newProgress;

    public void Dispose() => _temp.Dispose();

    private sealed class FirstCallBlockingGame : IGameProcess
    {
        private int _calls;

        public bool Running { get; set; }

        public ManualResetEventSlim FirstCallEntered { get; } = new();

        public ManualResetEventSlim ReleaseFirstCall { get; } = new();

        public bool IsRunning()
        {
            var observed = Running;
            if (Interlocked.Increment(ref _calls) == 1)
            {
                FirstCallEntered.Set();
                ReleaseFirstCall.Wait(TimeSpan.FromSeconds(10));
            }
            return observed;
        }
    }

    private sealed class ThrowsOnFirstCallGame : IGameProcess
    {
        private int _calls;

        public bool Running { get; set; }

        public bool IsRunning() => Interlocked.Increment(ref _calls) == 1 ? throw new InvalidOperationException("probe failed") : Running;
    }

    private async Task<GameWatcher> NewWatcherAsync(IGameProcess game, FakeTimeProvider? time = null, Action<Exception>? onError = null)
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: true);
        var watcher = new GameWatcher(game, _gate, rig.Cache, time ?? new FakeTimeProvider(), onError);
        watcher.RunningChanged += _runningChanges.Add;
        return watcher;
    }

    private async Task AdvanceUntilAsync(FakeTimeProvider time, Func<bool> condition)
    {
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (!condition() && DateTime.UtcNow < deadline)
        {
            time.Advance(GameWatcher.PollInterval);
            await Task.Delay(20);
        }
    }

    [Fact]
    public async Task A_stale_running_observation_cannot_overwrite_a_later_stopped_one()
    {
        var game = new FirstCallBlockingGame { Running = true };
        var watcher = await NewWatcherAsync(game);
        var first = Task.Run(() => watcher.TickAsync(CancellationToken.None));
        Assert.True(game.FirstCallEntered.Wait(TimeSpan.FromSeconds(10)));

        game.Running = false;
        var second = Task.Run(() => watcher.TickAsync(CancellationToken.None));
        await Task.Delay(100);
        game.ReleaseFirstCall.Set();
        await Task.WhenAll(first, second);

        Assert.Equal([true, false], _runningChanges);
        Assert.False(watcher.IsRunning);
    }

    [Fact]
    public async Task Game_restarting_while_the_watcher_waits_for_the_gate_skips_the_refresh_and_the_message()
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: true);
        var statePath = Path.Combine(_temp.Path, "state", "state.json");
        Directory.CreateDirectory(Path.GetDirectoryName(statePath)!);
        await File.WriteAllTextAsync(statePath, StatusRig.LastSyncJson("switch->pc").Replace("sha256:abc", Fingerprint.OfDirectory(rig.SaveDir)));
        await rig.Cache.RefreshAsync();
        var watcher = new GameWatcher(rig.Game, _gate, rig.Cache, new FakeTimeProvider());
        watcher.PcSaveHasNewProgress += () => _newProgress++;
        rig.Game.Running = true;
        await watcher.TickAsync(CancellationToken.None);
        File.WriteAllText(Path.Combine(rig.SaveDir, "slot_00", "progress.sav"), "played");
        var before = rig.Cache.Current;
        _gate.TryEnter(out var write);
        rig.Game.Running = false;
        var tick = watcher.TickAsync(CancellationToken.None);
        await Task.Delay(100);

        rig.Game.Running = true;
        write.Dispose();
        await tick;

        Assert.Equal(0, _newProgress);
        Assert.Same(before, rig.Cache.Current);
    }

    [Fact]
    public async Task A_probe_that_throws_once_is_reported_and_later_polls_still_detect_transitions()
    {
        var game = new ThrowsOnFirstCallGame();
        var errors = new List<Exception>();
        var time = new FakeTimeProvider();
        using var watcher = await NewWatcherAsync(game, time, errors.Add);
        var loop = watcher.Start();
        await Task.Delay(100);

        game.Running = true;
        await AdvanceUntilAsync(time, () => _runningChanges.Count > 0);
        watcher.Dispose();
        await loop;

        Assert.Equal([true], _runningChanges);
        Assert.Equal("probe failed", Assert.Single(errors).Message);
    }

    [Fact]
    public async Task Disposing_the_watcher_does_not_report_cancellation_as_an_error()
    {
        var errors = new List<Exception>();
        using var watcher = await NewWatcherAsync(new FakeGameProcess(), onError: errors.Add);
        var loop = watcher.Start();
        await Task.Delay(100);

        watcher.Dispose();
        await loop;

        Assert.Empty(errors);
    }

    [Fact]
    public async Task Post_exit_refresh_reads_the_real_save_even_though_the_cache_guards_while_the_game_runs()
    {
        var gameDir = Path.Combine(_temp.Path, "game");
        var saveDir = SaveTreeBuilder.BuildGameDirWithSave(gameDir, "A");
        var game = new FakeGameProcess();
        var session = new SettingsSession(new SettingsStore(Path.Combine(_temp.Path, "settings.json")), new LauncherSettings(GameDir: gameDir));
        var cache = new SaveStatusCache(session, new SaveStatusReader(new SyncStateStore(Path.Combine(_temp.Path, "state", "state.json"))), game);
        await cache.RefreshAsync();
        var watcher = new GameWatcher(game, _gate, cache, new FakeTimeProvider());
        game.Running = true;
        await watcher.TickAsync(CancellationToken.None);
        File.WriteAllText(Path.Combine(saveDir, "slot_00", "progress.sav"), "played");

        game.Running = false;
        await watcher.TickAsync(CancellationToken.None);

        var found = Assert.IsType<SaveStatus.Found>(cache.Current);
        Assert.Equal(Fingerprint.OfDirectory(saveDir), found.Snapshot.Fingerprint);
    }
}
