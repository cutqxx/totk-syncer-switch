using System.ComponentModel;
using System.Diagnostics;
using TotkSync.Core.Settings;
using TotkSync.Core.Sync;

namespace TotkSync.Core.Game;

public sealed class GameLauncher(IGameProcess game, TransferGate gate, Action<ProcessStartInfo> start)
{
    private readonly CancellationTokenSource _closing = new();

    public LaunchPhase Phase { get; private set; } = LaunchPhase.Idle;

    public void CancelPendingAndRefuseNew() => _closing.Cancel();

    public event Action<LaunchPhase>? PhaseChanged;

    public async Task<LaunchResult> PlayAsync(string gameDir, CancellationToken ct)
    {
        if (_closing.IsCancellationRequested)
        {
            return new LaunchResult.Cancelled();
        }
        try
        {
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct, _closing.Token);
            using var lease = await EnterGateAsync(linked.Token);
            return _closing.IsCancellationRequested ? new LaunchResult.Cancelled() : StartUnlessRunning(gameDir);
        }
        catch (OperationCanceledException)
        {
            return new LaunchResult.Cancelled();
        }
        finally
        {
            Publish(LaunchPhase.Idle);
        }
    }

    private async Task<IDisposable> EnterGateAsync(CancellationToken ct)
    {
        if (gate.TryEnter(out var lease))
        {
            return lease;
        }
        Publish(LaunchPhase.WaitingForSaveTransfer);
        return await gate.EnterAsync(ct);
    }

    private LaunchResult StartUnlessRunning(string gameDir)
    {
        if (game.IsRunning())
        {
            return new LaunchResult.AlreadyRunning();
        }
        Publish(LaunchPhase.Starting);
        return TryStart(gameDir);
    }

    private LaunchResult TryStart(string gameDir)
    {
        try
        {
            start(BuildStartInfo(gameDir));
            return new LaunchResult.Started();
        }
        catch (Exception error) when (error is Win32Exception or InvalidOperationException or IOException)
        {
            return new LaunchResult.Failed(error.Message);
        }
    }

    private static ProcessStartInfo BuildStartInfo(string gameDir)
    {
        return new ProcessStartInfo(Path.Combine(gameDir, SettingsValidator.GameExecutable))
        {
            WorkingDirectory = gameDir,
            UseShellExecute = false,
        };
    }

    private void Publish(LaunchPhase phase)
    {
        if (Phase == phase)
        {
            return;
        }
        Phase = phase;
        PhaseChanged?.Invoke(phase);
    }
}
