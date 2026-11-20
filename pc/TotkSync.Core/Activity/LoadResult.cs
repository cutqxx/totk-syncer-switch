namespace TotkSync.Core.Activity;

public abstract record LoadResult
{
    private LoadResult()
    {
    }

    public sealed record Loaded(int Count) : LoadResult;

    public sealed record Unreadable(string Reason) : LoadResult;
}
