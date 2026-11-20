using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class StatusQueryTests : IDisposable
{
    private readonly TempDir _temp = new();

    public void Dispose() => _temp.Dispose();

    [Fact]
    public async Task Game_running_follows_the_live_process_on_every_call()
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: false);

        var before = rig.Query.Get().GameRunning;
        rig.Game.Running = true;
        var during = rig.Query.Get().GameRunning;
        rig.Game.Running = false;
        var after = rig.Query.Get().GameRunning;

        Assert.Equal([false, true, false], [before, during, after]);
    }

    [Fact]
    public async Task Found_save_exposes_snapshot_and_last_sync()
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: true, StatusRig.LastSyncJson("switch->pc"));

        var status = rig.Query.Get();

        Assert.Equal(2, status.Save!.Files);
        Assert.Equal(SyncDirection.SwitchToPc, status.LastSync!.Direction);
    }

    [Fact]
    public async Task Found_save_without_sync_history_has_no_last_sync()
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: true);

        var status = rig.Query.Get();

        Assert.NotNull(status.Save);
        Assert.Null(status.LastSync);
    }

    [Fact]
    public async Task Configured_game_without_save_has_no_save_and_no_last_sync()
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: false, StatusRig.LastSyncJson("pc->switch"));

        var status = rig.Query.Get();

        Assert.Equal((null, null), (status.Save, status.LastSync));
    }

    [Fact]
    public void Cache_that_was_never_refreshed_reports_no_save_instead_of_failing()
    {
        var session = new SettingsSession(new SettingsStore(_temp.Combine("settings.json")), new LauncherSettings());
        var cache = new SaveStatusCache(session, new SaveStatusReader(new SyncStateStore(_temp.Combine("state.json"))));
        var query = new StatusQuery(cache, new FakeGameProcess { Running = true });

        var status = query.Get();

        Assert.Equal((true, null), (status.GameRunning, status.Save));
    }
}
