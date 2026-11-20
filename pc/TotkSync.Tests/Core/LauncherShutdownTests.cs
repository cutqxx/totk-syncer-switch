using TotkSync.Core.Game;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class LauncherShutdownTests
{
    private readonly TransferGate _gate = new();
    private readonly List<string> _events = [];

    private LauncherShutdown NewShutdown()
    {
        return new LauncherShutdown(_gate, () => { _events.Add("stopServer"); return Task.CompletedTask; }, () => _events.Add("cancelPending"));
    }

    [Fact]
    public async Task Idle_shutdown_cancels_pending_launch_then_stops_the_server_without_the_waiting_callback()
    {
        await NewShutdown().ShutdownAsync(() => _events.Add("waiting"));

        Assert.Equal(["cancelPending", "stopServer"], _events);
    }

    [Fact]
    public async Task Shutdown_during_a_write_reports_waiting_and_stops_the_server_only_after_the_write_ends()
    {
        _gate.TryEnter(out var write);

        var shutdown = NewShutdown().ShutdownAsync(() => _events.Add("waiting"));
        await Task.Delay(100);
        var eventsWhileWriting = _events.ToArray();
        write.Dispose();
        await shutdown;

        Assert.Equal(["cancelPending", "waiting"], eventsWhileWriting);
        Assert.Equal(["cancelPending", "waiting", "stopServer"], _events);
    }

    [Fact]
    public async Task Shutdown_keeps_the_gate_so_nothing_else_can_start_afterwards()
    {
        await NewShutdown().ShutdownAsync(() => { });

        Assert.True(_gate.IsBusy);
    }

    [Fact]
    public async Task Closing_during_a_write_with_a_queued_play_cancels_it_and_stops_the_server_once()
    {
        var starts = 0;
        var launcher = new GameLauncher(new FakeGameProcess(), _gate, _ => starts++);
        var stops = 0;
        var shutdown = new LauncherShutdown(_gate, () => { stops++; return Task.CompletedTask; }, launcher.CancelPendingAndRefuseNew);
        _gate.TryEnter(out var write);
        var play = launcher.PlayAsync(@"C:\g", CancellationToken.None);
        await WaitUntilAsync(() => launcher.Phase == LaunchPhase.WaitingForSaveTransfer);

        var closing = shutdown.ShutdownAsync(() => { });
        var result = await play;
        write.Dispose();
        await closing;

        Assert.IsType<LaunchResult.Cancelled>(result);
        Assert.Equal((0, 1, LaunchPhase.Idle), (starts, stops, launcher.Phase));
    }

    [Fact]
    public async Task Play_after_shutdown_began_is_Cancelled_and_does_not_start()
    {
        var starts = 0;
        var launcher = new GameLauncher(new FakeGameProcess(), _gate, _ => starts++);
        await new LauncherShutdown(_gate, () => Task.CompletedTask, launcher.CancelPendingAndRefuseNew).ShutdownAsync(() => { });

        var result = await launcher.PlayAsync(@"C:\g", CancellationToken.None);

        Assert.IsType<LaunchResult.Cancelled>(result);
        Assert.Equal(0, starts);
    }

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (!condition() && DateTime.UtcNow < deadline)
        {
            await Task.Delay(10);
        }
        Assert.True(condition(), "condition was not met in time");
    }
}
