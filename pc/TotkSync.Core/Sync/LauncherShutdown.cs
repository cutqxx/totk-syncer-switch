namespace TotkSync.Core.Sync;

public sealed class LauncherShutdown(TransferGate gate, Func<Task> stopServer, Action cancelPendingLaunch)
{
    public async Task ShutdownAsync(Action onWaitingForWrite)
    {
        cancelPendingLaunch();
        if (gate.IsBusy)
        {
            onWaitingForWrite();
        }
        await gate.EnterAsync(CancellationToken.None);
        await stopServer();
    }
}
