namespace TotkSync.Core.Sync;

public abstract record SendResult
{
    private SendResult()
    {
    }

    public sealed record Sent(byte[] Zip, string Fingerprint, DateTimeOffset Modified) : SendResult;

    public sealed record NoSave : SendResult;

    public sealed record GameRunning : SendResult;

    public sealed record Busy : SendResult;

    public sealed record Failed(string Reason) : SendResult;
}

public abstract record AckResult
{
    private AckResult()
    {
    }

    public sealed record Recorded : AckResult;

    public sealed record Mismatch : AckResult;

    public sealed record NoSave : AckResult;

    public sealed record GameRunning : AckResult;

    public sealed record Busy : AckResult;

    public sealed record Failed(string Reason) : AckResult;
}
