using TotkSync.Core.Saves;
using TotkSync.Core.Sync;

namespace TotkSync.Core.Status;

public abstract record SaveStatus
{
    private SaveStatus()
    {
    }

    public sealed record NotConfigured : SaveStatus;

    public sealed record NotFound : SaveStatus;

    public sealed record GameRunning : SaveStatus;

    public sealed record Several(IReadOnlyList<SaveCandidate> Items) : SaveStatus;

    public sealed record Found(SaveSnapshot Snapshot, LastSync? LastSync) : SaveStatus
    {
        public SyncStatus Sync => SyncStatusEvaluator.Evaluate(Snapshot, LastSync);
    }

    public sealed record Unreadable(string Reason) : SaveStatus;
}
