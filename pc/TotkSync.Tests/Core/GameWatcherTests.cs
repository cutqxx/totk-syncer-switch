using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Game;
using TotkSync.Core.Saves;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class GameWatcherTests : IDisposable
{
    private readonly TempDir _temp = new();
    private readonly TransferGate _gate = new();
    private readonly List<bool> _runningChanges = [];
    private int _newProgress;

    public void Dispose() => _temp.Dispose();

    private async Task<(GameWatcher Watcher, StatusRig Rig)> NewWatcherAsync(bool synced)
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: true);
        if (synced)
        {
            await MarkSavedAsSyncedAsync(rig);
        }
        var watcher = new GameWatcher(rig.Game, _gate, rig.Cache, new FakeTimeProvider());
        watcher.RunningChanged += _runningChanges.Add;
        watcher.PcSaveHasNewProgress += () => _newProgress++;
        return (watcher, rig);
    }

    private async Task MarkSavedAsSyncedAsync(StatusRig rig)
    {
        var statePath = Path.Combine(_temp.Path, "state", "state.json");
        Directory.CreateDirectory(Path.GetDirectoryName(statePath)!);
        var json = StatusRig.LastSyncJson("switch->pc").Replace("sha256:abc", Fingerprint.OfDirectory(rig.SaveDir));
        await File.WriteAllTextAsync(statePath, json);
        await rig.Cache.RefreshAsync();
    }

    private static void ChangeSave(StatusRig rig) => File.WriteAllText(Path.Combine(rig.SaveDir, "slot_00", "progress.sav"), "played " + Guid.NewGuid());

    [Fact]
    public async Task Game_starting_raises_RunningChanged_true_once_and_unchanged_polls_stay_quiet()
    {
        var (watcher, rig) = await NewWatcherAsync(synced: true);

        rig.Game.Running = true;
        await watcher.TickAsync(CancellationToken.None);
        await watcher.TickAsync(CancellationToken.None);

        Assert.Equal([true], _runningChanges);
        Assert.True(watcher.IsRunning);
    }

    [Fact]
    public async Task Game_exit_with_changed_save_raises_new_progress_after_refreshing_the_status()
    {
        var (watcher, rig) = await NewWatcherAsync(synced: true);
        rig.Game.Running = true;
        await watcher.TickAsync(CancellationToken.None);
        ChangeSave(rig);

        rig.Game.Running = false;
        await watcher.TickAsync(CancellationToken.None);

        Assert.Equal([true, false], _runningChanges);
        Assert.Equal(1, _newProgress);
    }

    [Fact]
    public async Task Game_exit_with_unchanged_save_raises_no_new_progress()
    {
        var (watcher, rig) = await NewWatcherAsync(synced: true);
        rig.Game.Running = true;
        await watcher.TickAsync(CancellationToken.None);

        rig.Game.Running = false;
        await watcher.TickAsync(CancellationToken.None);

        Assert.Equal(0, _newProgress);
    }

    [Fact]
    public async Task Game_exit_when_never_synced_raises_no_new_progress()
    {
        var (watcher, rig) = await NewWatcherAsync(synced: false);
        rig.Game.Running = true;
        await watcher.TickAsync(CancellationToken.None);
        ChangeSave(rig);

        rig.Game.Running = false;
        await watcher.TickAsync(CancellationToken.None);

        Assert.Equal(0, _newProgress);
    }

    [Fact]
    public async Task Game_start_never_raises_new_progress_even_if_the_save_differs()
    {
        var (watcher, rig) = await NewWatcherAsync(synced: true);
        ChangeSave(rig);

        rig.Game.Running = true;
        await watcher.TickAsync(CancellationToken.None);

        Assert.Equal(0, _newProgress);
    }

    [Fact]
    public async Task Refresh_after_game_exit_waits_for_a_write_in_progress()
    {
        var (watcher, rig) = await NewWatcherAsync(synced: true);
        rig.Game.Running = true;
        await watcher.TickAsync(CancellationToken.None);
        ChangeSave(rig);
        _gate.TryEnter(out var write);
        rig.Game.Running = false;

        var tick = watcher.TickAsync(CancellationToken.None);
        await Task.Delay(100);
        var raisedWhileWriting = _newProgress;
        write.Dispose();
        await tick;

        Assert.Equal((0, 1), (raisedWhileWriting, _newProgress));
    }

    [Fact]
    public async Task Running_loop_picks_up_the_game_on_the_next_poll_interval()
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: true);
        var time = new FakeTimeProvider();
        using var watcher = new GameWatcher(rig.Game, _gate, rig.Cache, time);
        watcher.RunningChanged += _runningChanges.Add;
        var loop = watcher.Start();
        await Task.Delay(100);

        rig.Game.Running = true;
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (_runningChanges.Count == 0 && DateTime.UtcNow < deadline)
        {
            time.Advance(GameWatcher.PollInterval);
            await Task.Delay(20);
        }
        watcher.Dispose();
        await loop;

        Assert.Equal([true], _runningChanges);
    }
}
