namespace TotkSync.Core.Sync;

public abstract record ReceiveResult
{
    private ReceiveResult()
    {
    }

    public sealed record Replaced(string BackupName, string Fingerprint) : ReceiveResult;

    public sealed record Unchanged(string Fingerprint) : ReceiveResult;

    public sealed record Conflict(DateTimeOffset PcModified, DateTimeOffset? LastSyncAt) : ReceiveResult;

    public sealed record LengthRequired : ReceiveResult;

    public sealed record TooLarge : ReceiveResult;

    public sealed record InvalidZip(string Reason) : ReceiveResult;

    public sealed record NoSave : ReceiveResult;

    public sealed record GameRunning : ReceiveResult;

    public sealed record Busy : ReceiveResult;

    public sealed record Failed(string Reason, bool IsSaveChanged = false) : ReceiveResult;
}
