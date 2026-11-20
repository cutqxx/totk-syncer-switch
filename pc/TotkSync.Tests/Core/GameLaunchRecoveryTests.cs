using TotkSync.Core.Game;
using TotkSync.Core.Replace;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class GameLaunchRecoveryTests
{
    private const string GameDir = @"C:\Games\TotK";

    private readonly FakeGameProcess _game = new();
    private readonly TransferGate _gate = new();
    private readonly List<string> _events = [];
    private readonly List<bool> _gateBusyDuringRecovery = [];
    private Action _recoverBody = () => { };

    private PendingRecovery NewRecovery()
    {
        return new PendingRecovery(_game, _gate, () =>
        {
            _events.Add("recover");
            _gateBusyDuringRecovery.Add(_gate.IsBusy);
            _recoverBody();
            return [];
        }, _ => { });
    }

    private GameLauncher NewLauncher(PendingRecovery? recovery) => new(_game, _gate, _ => _events.Add("start"), recovery);

    [Fact]
    public async Task Play_runs_recovery_once_under_the_lease_and_before_the_game_starts()
    {
        var launcher = NewLauncher(NewRecovery());

        var result = await launcher.PlayAsync(GameDir, CancellationToken.None);

        Assert.IsType<LaunchResult.Started>(result);
        Assert.Equal(["recover", "start"], _events);
        Assert.Equal([true], _gateBusyDuringRecovery);
    }

    [Fact]
    public async Task A_second_play_does_not_run_recovery_again()
    {
        var launcher = NewLauncher(NewRecovery());

        await launcher.PlayAsync(GameDir, CancellationToken.None);
        await launcher.PlayAsync(GameDir, CancellationToken.None);

        Assert.Equal(["recover", "start", "start"], _events);
    }

    [Fact]
    public async Task A_save_write_cannot_take_the_gate_while_recovery_runs()
    {
        using var inRecovery = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        _recoverBody = () =>
        {
            inRecovery.Set();
            release.Wait(TimeSpan.FromSeconds(10));
        };
        var play = NewLauncher(NewRecovery()).PlayAsync(GameDir, CancellationToken.None);
        Assert.True(inRecovery.Wait(TimeSpan.FromSeconds(10)));

        var writeCouldEnter = _gate.TryEnter(out var lease);
        release.Set();
        await play;

        Assert.False(writeCouldEnter);
        lease.Dispose();
    }

    [Fact]
    public async Task Game_already_running_skips_recovery_and_does_not_start_again()
    {
        _game.Running = true;

        var result = await NewLauncher(NewRecovery()).PlayAsync(GameDir, CancellationToken.None);

        Assert.IsType<LaunchResult.AlreadyRunning>(result);
        Assert.Empty(_events);
    }

    [Fact]
    public async Task Closing_during_recovery_gives_Cancelled_without_starting_and_frees_the_gate()
    {
        using var inRecovery = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        _recoverBody = () =>
        {
            inRecovery.Set();
            release.Wait(TimeSpan.FromSeconds(10));
        };
        var launcher = NewLauncher(NewRecovery());
        var play = launcher.PlayAsync(GameDir, CancellationToken.None);
        Assert.True(inRecovery.Wait(TimeSpan.FromSeconds(10)));

        launcher.CancelPendingAndRefuseNew();
        release.Set();
        var result = await play;

        Assert.IsType<LaunchResult.Cancelled>(result);
        Assert.Equal(["recover"], _events);
        Assert.False(_gate.IsBusy);
        Assert.Equal(LaunchPhase.Idle, launcher.Phase);
    }

    [Fact]
    public async Task Without_a_recovery_play_just_starts_the_game()
    {
        var result = await NewLauncher(null).PlayAsync(GameDir, CancellationToken.None);

        Assert.IsType<LaunchResult.Started>(result);
        Assert.Equal(["start"], _events);
    }

    [Fact]
    public async Task Slow_recovery_does_not_block_the_calling_thread()
    {
        using var inRecovery = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var recoveryOnPool = false;
        _recoverBody = () =>
        {
            recoveryOnPool = Thread.CurrentThread.IsThreadPoolThread;
            inRecovery.Set();
            release.Wait(TimeSpan.FromSeconds(10));
        };
        var clock = System.Diagnostics.Stopwatch.StartNew();

        var play = NewLauncher(NewRecovery()).PlayAsync(GameDir, CancellationToken.None);
        var returnedAfter = clock.Elapsed;
        var isStillRunning = !play.IsCompleted;
        Assert.True(inRecovery.Wait(TimeSpan.FromSeconds(10)));
        release.Set();
        await play;

        Assert.True(returnedAfter < TimeSpan.FromSeconds(5), $"PlayAsync blocked the caller for {returnedAfter}");
        Assert.True(isStillRunning);
        Assert.True(recoveryOnPool);
    }

    [Fact]
    public async Task Real_recovery_restores_a_missing_save_from_its_orphaned_old_folder_before_the_game_starts()
    {
        using var rig = await ReceiverRig.CreateAsync();
        var before = rig.SaveTree();
        Directory.Move(rig.SaveDir, Path.Combine(rig.ProfileDir, "A.totksync-old-0a1b2c3d"));
        var recovery = new PendingRecovery(rig.Game, rig.Gate, () => rig.RealRecovery().Recover(rig.ProfileDir), _ => { });
        var saveRestoredAtStart = new List<bool>();
        var launcher = new GameLauncher(rig.Game, rig.Gate, _ => saveRestoredAtStart.Add(Directory.Exists(rig.SaveDir)), recovery);

        var result = await launcher.PlayAsync(GameDir, CancellationToken.None);

        Assert.IsType<LaunchResult.Started>(result);
        Assert.Equal([true], saveRestoredAtStart);
        Assert.Equal(before, rig.SaveTree());
        Assert.False(recovery.IsPending);
    }
}
