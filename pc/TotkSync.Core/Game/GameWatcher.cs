using TotkSync.Core.Replace;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;

namespace TotkSync.Core.Game;

public sealed class GameWatcher(
    IGameProcess game,
    TransferGate gate,
    SaveStatusCache cache,
    TimeProvider time,
    Action<Exception>? onError = null,
    PendingRecovery? recovery = null) : IDisposable
{
    public static readonly TimeSpan PollInterval = TimeSpan.FromSeconds(2);

    private readonly CancellationTokenSource _stop = new();
    private readonly SemaphoreSlim _tickLock = new(1, 1);
    private volatile bool _wasRunning;
    private string? _lastErrorMessage;

    public bool IsRunning => _wasRunning;

    public event Action<bool>? RunningChanged;

    public event Action? PcSaveHasNewProgress;

    public Task Start() => Task.Run(() => RunAsync(_stop.Token));

    public void Dispose() => _stop.Cancel();

    public async Task RunAsync(CancellationToken ct)
    {
        using var timer = new PeriodicTimer(PollInterval, time);
        try
        {
            await TickSurvivingErrorsAsync(ct);
            while (await timer.WaitForNextTickAsync(ct))
            {
                await TickSurvivingErrorsAsync(ct);
            }
        }
        catch (OperationCanceledException)
        {
        }
    }

    private async Task TickSurvivingErrorsAsync(CancellationToken ct)
    {
        try
        {
            await TickAsync(ct);
            _lastErrorMessage = null;
        }
        catch (Exception error) when (!ct.IsCancellationRequested)
        {
            ReportOnce(error);
        }
    }

    private void ReportOnce(Exception error)
    {
        if (error.Message == _lastErrorMessage)
        {
            return;
        }
        _lastErrorMessage = error.Message;
        onError?.Invoke(error);
    }

    public async Task TickAsync(CancellationToken ct)
    {
        await _tickLock.WaitAsync(ct);
        try
        {
            await ObserveAsync(ct);
        }
        finally
        {
            _tickLock.Release();
        }
    }

    private async Task ObserveAsync(CancellationToken ct)
    {
        var isRunning = game.IsRunning();
        var isTransition = isRunning != _wasRunning;
        _wasRunning = isRunning;
        if (isTransition)
        {
            RunningChanged?.Invoke(isRunning);
        }
        if (!isRunning && (isTransition || HasWorkDeferredByGame))
        {
            await ReportNewProgressAsync(ct);
        }
    }

    private bool HasWorkDeferredByGame => recovery?.IsPending == true || cache.Current is SaveStatus.GameRunning;

    private async Task ReportNewProgressAsync(CancellationToken ct)
    {
        using (await gate.EnterAsync(ct))
        {
            if (game.IsRunning())
            {
                return;
            }
            recovery?.RunHoldingGate();
            await cache.RefreshAsync();
        }
        if (cache.Current is SaveStatus.Found { Sync: SyncStatus.PcHasNewProgress })
        {
            PcSaveHasNewProgress?.Invoke();
        }
    }
}
