namespace TotkSync.Core.Replace;

public sealed record SwapRetry(int Attempts, TimeSpan Delay)
{
    public static SwapRetry Default { get; } = new(5, TimeSpan.FromMilliseconds(200));

    public static SwapRetry None { get; } = new(1, TimeSpan.Zero);

    public Exception? Run(Action action)
    {
        Exception? lastFailure = null;
        for (var attempt = 1; attempt <= Attempts; attempt++)
        {
            try
            {
                action();
                return null;
            }
            catch (Exception failure)
            {
                lastFailure = failure;
                if (attempt < Attempts)
                {
                    Thread.Sleep(Delay);
                }
            }
        }
        return lastFailure;
    }
}
