using TotkSync.Core.Replace;

namespace TotkSync.Tests.Core;

public sealed class SwapRetryTests
{
    [Fact]
    public void Successful_action_runs_once_and_reports_no_failure()
    {
        var calls = 0;

        var failure = new SwapRetry(3, TimeSpan.Zero).Run(() => calls++);

        Assert.Equal((null, 1), (failure, calls));
    }

    [Fact]
    public void Action_that_recovers_on_the_second_attempt_is_a_success()
    {
        var calls = 0;

        var failure = new SwapRetry(3, TimeSpan.Zero).Run(() =>
        {
            if (++calls < 2)
            {
                throw new IOException("busy");
            }
        });

        Assert.Equal((null, 2), (failure, calls));
    }

    [Fact]
    public void Action_that_keeps_failing_is_tried_attempts_times_and_the_last_failure_is_returned()
    {
        var calls = 0;

        var failure = new SwapRetry(3, TimeSpan.Zero).Run(() => throw new IOException($"failure {++calls}"));

        Assert.Equal((3, "failure 3"), (calls, failure!.Message));
    }

    [Fact]
    public void None_tries_exactly_once_without_waiting()
    {
        var calls = 0;

        var failure = SwapRetry.None.Run(() => throw new IOException($"failure {++calls}"));

        Assert.Equal((1, "failure 1"), (calls, failure!.Message));
    }
}
