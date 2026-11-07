using TotkSync.Core.Saves;

namespace TotkSync.Core.Sync;

public enum SyncStatus
{
    NeverSynced,
    InSync,
    PcHasNewProgress,
}

public static class SyncStatusEvaluator
{
    public static SyncStatus Evaluate(SaveSnapshot snapshot, LastSync? lastSync)
    {
        if (lastSync is null)
        {
            return SyncStatus.NeverSynced;
        }
        return lastSync.Fingerprint == snapshot.Fingerprint ? SyncStatus.InSync : SyncStatus.PcHasNewProgress;
    }
}
