namespace TotkSync.Core.Game;

public enum LaunchPhase
{
    Idle,
    WaitingForSaveTransfer,
    Starting,
}

public abstract record LaunchResult
{
    private LaunchResult()
    {
    }

    public sealed record Started : LaunchResult;

    public sealed record AlreadyRunning : LaunchResult;

    public sealed record Cancelled : LaunchResult;

    public sealed record Failed(string Reason) : LaunchResult;
}
