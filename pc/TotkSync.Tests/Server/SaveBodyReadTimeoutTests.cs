using TotkSync.Server;

namespace TotkSync.Tests.Server;

public class SaveBodyReadTimeoutTests
{
    [Theory]
    [InlineData(0L, 30)]
    [InlineData(65_536L, 31)]
    [InlineData(1L, 31)]
    [InlineData(65_537L, 32)]
    [InlineData(20_000_000L, 336)]
    [InlineData(64L * 1024 * 1024, 900)]
    [InlineData(1L << 40, 900)]
    public void Timeout_is_thirty_seconds_plus_one_second_per_started_64_KiB_capped_at_fifteen_minutes(long contentLength, int expectedSeconds)
    {
        Assert.Equal(TimeSpan.FromSeconds(expectedSeconds), SaveEndpoint.BodyReadTimeout(contentLength));
    }

    [Fact]
    public void A_slow_but_legitimate_big_upload_gets_more_time_than_the_old_fixed_two_minutes()
    {
        Assert.True(SaveEndpoint.BodyReadTimeout(20_000_000) > TimeSpan.FromMinutes(2));
    }
}
