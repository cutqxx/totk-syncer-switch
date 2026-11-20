using TotkSync.Core.Game;
using TotkSync.Core.Sync;

namespace TotkSync.Core.Replace;

public sealed class PendingRecovery(
    IGameProcess game,
    TransferGate gate,
    Func<IReadOnlyList<string>> recover,
    Action<IReadOnlyList<string>> report)
{
    private volatile bool _isDone;

    public bool IsPending => !_isDone;

    public async Task EnsureAsync(CancellationToken ct)
    {
        if (_isDone)
        {
            return;
        }
        using (await gate.EnterAsync(ct))
        {
            await Task.Run(RunHoldingGate, CancellationToken.None);
        }
    }

    public void RunHoldingGate()
    {
        if (_isDone || game.IsRunning())
        {
            return;
        }
        _isDone = true;
        report(recover());
    }
}
