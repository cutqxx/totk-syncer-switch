using TotkSync.Core.Replace;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class PendingRecoveryTests
{
    private readonly FakeGameProcess _game = new();
    private readonly TransferGate _gate = new();
    private readonly List<bool> _gateBusyDuringRecover = [];
    private readonly List<IReadOnlyList<string>> _reports = [];
    private int _recoverCalls;
    private Func<IReadOnlyList<string>> _recoverBody = () => ["note"];

    private PendingRecovery NewRecovery()
    {
        return new PendingRecovery(
            _game,
            _gate,
            () =>
            {
                _recoverCalls++;
                _gateBusyDuringRecover.Add(_gate.IsBusy);
                return _recoverBody();
            },
            _reports.Add);
    }

    [Fact]
    public async Task Recovery_is_pending_until_it_has_run()
    {
        var recovery = NewRecovery();

        Assert.True(recovery.IsPending);
        await recovery.EnsureAsync(CancellationToken.None);
        Assert.False(recovery.IsPending);
    }

    [Fact]
    public async Task Running_game_defers_recovery_and_leaves_it_pending()
    {
        _game.Running = true;
        var recovery = NewRecovery();

        await recovery.EnsureAsync(CancellationToken.None);

        Assert.Equal((0, true), (_recoverCalls, recovery.IsPending));
        Assert.Empty(_reports);
    }

    [Fact]
    public async Task Recovery_runs_once_the_game_has_stopped_under_the_gate_and_reports_its_notes()
    {
        _game.Running = true;
        var recovery = NewRecovery();
        await recovery.EnsureAsync(CancellationToken.None);

        _game.Running = false;
        await recovery.EnsureAsync(CancellationToken.None);

        Assert.Equal(1, _recoverCalls);
        Assert.Equal([true], _gateBusyDuringRecover);
        Assert.Equal(["note"], Assert.Single(_reports));
        Assert.False(_gate.IsBusy);
    }

    [Fact]
    public async Task Recovery_never_runs_a_second_time()
    {
        var recovery = NewRecovery();

        await recovery.EnsureAsync(CancellationToken.None);
        await recovery.EnsureAsync(CancellationToken.None);
        recovery.RunHoldingGate();

        Assert.Equal(1, _recoverCalls);
    }

    [Fact]
    public async Task A_recovery_that_throws_is_not_retried_and_releases_the_gate()
    {
        _recoverBody = () => throw new IOException("disk");
        var recovery = NewRecovery();

        await Assert.ThrowsAsync<IOException>(() => recovery.EnsureAsync(CancellationToken.None));
        await recovery.EnsureAsync(CancellationToken.None);

        Assert.Equal((1, false), (_recoverCalls, _gate.IsBusy));
        Assert.False(recovery.IsPending);
    }

    [Fact]
    public async Task Ensure_waits_for_a_write_in_progress_before_recovering()
    {
        _gate.TryEnter(out var write);
        var recovery = NewRecovery();

        var ensure = recovery.EnsureAsync(CancellationToken.None);
        await Task.Delay(100);
        var callsWhileWriting = _recoverCalls;
        write.Dispose();
        await ensure;

        Assert.Equal((0, 1), (callsWhileWriting, _recoverCalls));
    }
}
