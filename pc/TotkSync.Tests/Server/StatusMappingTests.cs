using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Saves;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Server;

namespace TotkSync.Tests.Server;

public class StatusMappingTests
{
    private static readonly TimeZoneInfo Plus3 = TimeZoneInfo.CreateCustomTimeZone("UTC+3", TimeSpan.FromHours(3), "UTC+3", "UTC+3");

    private static FakeTimeProvider TimeIn(TimeZoneInfo zone)
    {
        var time = new FakeTimeProvider();
        time.SetLocalTimeZone(zone);
        return time;
    }

    private static SaveSnapshot Snapshot(DateTimeOffset modified)
    {
        return new SaveSnapshot(@"C:\fake\save", modified, 27, 20123456, "sha256:abc");
    }

    [Fact]
    public void Save_modified_time_is_shown_in_local_zone_with_offset_and_no_fraction()
    {
        var modified = new DateTimeOffset(2026, 11, 3, 11, 25, 1, 987, TimeSpan.Zero).AddTicks(1234);
        var status = new LauncherStatus(false, Snapshot(modified), null);

        var response = StatusMapping.ToResponse(status, TimeIn(Plus3));

        Assert.Equal("2026-11-03T14:25:01+03:00", response.Save.Modified);
    }

    [Fact]
    public void Negative_offset_zone_is_formatted_with_minus_and_minutes()
    {
        var zone = TimeZoneInfo.CreateCustomTimeZone("UTC-3:30", -TimeSpan.FromMinutes(210), "x", "x");
        var status = new LauncherStatus(false, Snapshot(new DateTimeOffset(2026, 1, 2, 3, 0, 0, TimeSpan.Zero)), null);

        var response = StatusMapping.ToResponse(status, TimeIn(zone));

        Assert.Equal("2026-01-01T23:30:00-03:30", response.Save.Modified);
    }

    [Fact]
    public void Utc_zone_is_formatted_as_plus_zero_offset()
    {
        var status = new LauncherStatus(false, Snapshot(new DateTimeOffset(2026, 1, 2, 3, 4, 5, TimeSpan.FromHours(5))), null);

        var response = StatusMapping.ToResponse(status, TimeIn(TimeZoneInfo.Utc));

        Assert.Equal("2026-01-01T22:04:05+00:00", response.Save.Modified);
    }

    [Fact]
    public void Last_sync_time_and_direction_are_mapped()
    {
        var at = new DateTimeOffset(2026, 11, 3, 9, 0, 0, TimeSpan.Zero);
        var status = new LauncherStatus(false, null, new LastSync("sha256:abc", at, SyncDirection.PcToSwitch));

        var response = StatusMapping.ToResponse(status, TimeIn(Plus3));

        Assert.Equal(new LastSyncDto("sha256:abc", "2026-11-03T12:00:00+03:00", SyncDirection.PcToSwitch), response.LastSync);
    }

    [Fact]
    public void Missing_save_maps_to_not_present_with_no_details()
    {
        var response = StatusMapping.ToResponse(new LauncherStatus(false, null, null), TimeIn(Plus3));

        Assert.Equal(new SaveDto(Present: false), response.Save);
        Assert.Null(response.LastSync);
    }

    [Fact]
    public void Present_save_carries_files_bytes_and_fingerprint()
    {
        var status = new LauncherStatus(true, Snapshot(DateTimeOffset.UnixEpoch), null);

        var response = StatusMapping.ToResponse(status, TimeIn(Plus3));

        Assert.Equal((true, 27, 20123456L, "sha256:abc"), (response.Save.Present, response.Save.Files, response.Save.Bytes, response.Save.Fingerprint));
        Assert.True(response.GameRunning);
    }

    [Fact]
    public void App_name_and_api_version_are_fixed()
    {
        var response = StatusMapping.ToResponse(new LauncherStatus(false, null, null), TimeIn(Plus3));

        Assert.Equal(("totk-sync", 1), (response.App, response.Api));
    }
}
