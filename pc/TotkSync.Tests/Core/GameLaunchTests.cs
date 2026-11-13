using System.ComponentModel;
using System.Diagnostics;
using TotkSync.Core.Game;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class GameLaunchTests
{
    private const string GameDir = @"C:\Games\TotK";

    private readonly FakeGameProcess _game = new();
    private readonly TransferGate _gate = new();
    private readonly List<ProcessStartInfo> _started = [];
    private readonly List<LaunchPhase> _phases = [];
    private readonly List<bool> _gateBusyDuringStart = [];
    private Action<ProcessStartInfo>? _failWith;

    private GameLauncher NewLauncher()
    {
        var launcher = new GameLauncher(_game, _gate, info =>
        {
            _gateBusyDuringStart.Add(_gate.IsBusy);
            _failWith?.Invoke(info);
            _started.Add(info);
        });
        launcher.PhaseChanged += _phases.Add;
        return launcher;
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

    [Fact]
    public async Task Play_starts_totk_exe_from_the_game_folder_without_shell()
    {
        var result = await NewLauncher().PlayAsync(GameDir, CancellationToken.None);

        Assert.IsType<LaunchResult.Started>(result);
        var info = Assert.Single(_started);
        Assert.Equal((Path.Combine(GameDir, "totk.exe"), GameDir, false), (info.FileName, info.WorkingDirectory, info.UseShellExecute));
    }

    [Fact]
    public async Task Play_starts_the_process_while_holding_the_transfer_gate_and_releases_it_after()
    {
        await NewLauncher().PlayAsync(GameDir, CancellationToken.None);

        Assert.Equal([true], _gateBusyDuringStart);
        Assert.False(_gate.IsBusy);
    }

    [Fact]
    public async Task Play_goes_through_Starting_and_back_to_Idle()
    {
        var launcher = NewLauncher();

        await launcher.PlayAsync(GameDir, CancellationToken.None);

        Assert.Equal([LaunchPhase.Starting, LaunchPhase.Idle], _phases);
        Assert.Equal(LaunchPhase.Idle, launcher.Phase);
    }

    [Fact]
    public async Task Game_started_outside_the_launcher_gives_AlreadyRunning_and_does_not_start_again()
    {
        _game.Running = true;

        var result = await NewLauncher().PlayAsync(GameDir, CancellationToken.None);

        Assert.IsType<LaunchResult.AlreadyRunning>(result);
        Assert.Empty(_started);
        Assert.False(_gate.IsBusy);
    }

    [Fact]
    public async Task Game_that_started_while_waiting_for_the_gate_is_not_launched_twice()
    {
        var launcher = NewLauncher();
        _gate.TryEnter(out var lease);
        var play = launcher.PlayAsync(GameDir, CancellationToken.None);
        await WaitUntilAsync(() => launcher.Phase == LaunchPhase.WaitingForSaveTransfer);

        _game.Running = true;
        lease.Dispose();

        Assert.IsType<LaunchResult.AlreadyRunning>(await play);
        Assert.Empty(_started);
    }

    [Fact]
    public async Task Play_during_a_write_waits_and_does_not_start_until_the_lease_is_released()
    {
        var launcher = NewLauncher();
        _gate.TryEnter(out var lease);

        var play = launcher.PlayAsync(GameDir, CancellationToken.None);
        await WaitUntilAsync(() => launcher.Phase == LaunchPhase.WaitingForSaveTransfer);
        await Task.Delay(100);
        var startedWhileWriting = _started.Count;
        lease.Dispose();
        var result = await play;

        Assert.Equal(0, startedWhileWriting);
        Assert.IsType<LaunchResult.Started>(result);
        Assert.Equal([LaunchPhase.WaitingForSaveTransfer, LaunchPhase.Starting, LaunchPhase.Idle], _phases);
    }

    [Fact]
    public async Task Process_start_failure_gives_Failed_with_the_reason_and_frees_the_gate()
    {
        _failWith = _ => throw new Win32Exception("Access is denied");

        var result = await NewLauncher().PlayAsync(GameDir, CancellationToken.None);

        Assert.Equal("Access is denied", Assert.IsType<LaunchResult.Failed>(result).Reason);
        Assert.False(_gate.IsBusy);
    }

    [Theory]
    [MemberData(nameof(RecoverableStartErrors))]
    public async Task Other_start_errors_also_give_Failed_instead_of_throwing(Exception error)
    {
        _failWith = _ => throw error;

        var result = await NewLauncher().PlayAsync(GameDir, CancellationToken.None);

        Assert.Equal(error.Message, Assert.IsType<LaunchResult.Failed>(result).Reason);
    }

    public static TheoryData<Exception> RecoverableStartErrors() => new()
    {
        new InvalidOperationException("no file name"),
        new IOException("disk gone"),
    };

    [Fact]
    public async Task Caller_cancellation_while_waiting_gives_Cancelled_and_never_starts()
    {
        var launcher = NewLauncher();
        _gate.TryEnter(out var lease);
        using var cts = new CancellationTokenSource();
        var play = launcher.PlayAsync(GameDir, cts.Token);
        await WaitUntilAsync(() => launcher.Phase == LaunchPhase.WaitingForSaveTransfer);

        cts.Cancel();

        Assert.IsType<LaunchResult.Cancelled>(await play);
        lease.Dispose();
        Assert.Empty(_started);
        Assert.Equal(LaunchPhase.Idle, launcher.Phase);
    }

    [Fact]
    public async Task Closing_cancels_a_queued_play_so_the_game_never_starts_and_phase_returns_to_Idle()
    {
        var launcher = NewLauncher();
        _gate.TryEnter(out var lease);
        var play = launcher.PlayAsync(GameDir, CancellationToken.None);
        await WaitUntilAsync(() => launcher.Phase == LaunchPhase.WaitingForSaveTransfer);

        launcher.CancelPendingAndRefuseNew();
        var result = await play;
        lease.Dispose();

        Assert.IsType<LaunchResult.Cancelled>(result);
        Assert.Empty(_started);
        Assert.Equal(LaunchPhase.Idle, launcher.Phase);
    }

    [Fact]
    public async Task Play_after_closing_began_is_Cancelled_immediately_without_starting()
    {
        var launcher = NewLauncher();
        launcher.CancelPendingAndRefuseNew();

        var result = await launcher.PlayAsync(GameDir, CancellationToken.None);

        Assert.IsType<LaunchResult.Cancelled>(result);
        Assert.Empty(_started);
        Assert.Empty(_phases);
    }
}
