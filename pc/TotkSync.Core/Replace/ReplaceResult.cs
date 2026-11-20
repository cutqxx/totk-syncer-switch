namespace TotkSync.Core.Replace;

public abstract record ReplaceResult
{
    private ReplaceResult()
    {
    }

    public sealed record Replaced : ReplaceResult;

    public sealed record GameRunning : ReplaceResult;

    public sealed record Failed(string Reason) : ReplaceResult;
}
