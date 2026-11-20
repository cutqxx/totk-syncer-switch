using TotkSync.Core.Display;
using TotkSync.Core.Sync;

namespace TotkSync.Tests.Core;

public class DisplayFormatTests
{
    private static readonly DateTimeOffset LocalTime =
        new(new DateTime(2026, 11, 3, 14, 25, 59, DateTimeKind.Local));

    [Fact]
    public void Date_uses_local_time_without_seconds()
    {
        Assert.Equal("2026-11-03 14:25", DisplayFormat.Date(LocalTime));
    }

    [Fact]
    public void Date_converts_utc_input_to_local_time()
    {
        Assert.Equal("2026-11-03 14:25", DisplayFormat.Date(LocalTime.ToUniversalTime()));
    }

    [Theory]
    [InlineData(0, "0 B")]
    [InlineData(999, "999 B")]
    [InlineData(1_000, "1.0 KB")]
    [InlineData(1_500, "1.5 KB")]
    [InlineData(999_999, "1000.0 KB")]
    [InlineData(1_000_000, "1.0 MB")]
    [InlineData(19_200_000, "19.2 MB")]
    public void Size_uses_decimal_units_with_one_fraction_digit(long bytes, string expected)
    {
        Assert.Equal(expected, DisplayFormat.Size(bytes));
    }

    [Fact]
    public void Files_and_size_matches_design_example()
    {
        Assert.Equal("27 files, 19.2 MB", DisplayFormat.FilesAndSize(27, 19_200_000));
    }

    [Fact]
    public void Files_and_size_uses_singular_for_one_file()
    {
        Assert.Equal("1 file, 8 B", DisplayFormat.FilesAndSize(1, 8));
    }

    [Fact]
    public void Files_and_size_uses_plural_for_zero_files()
    {
        Assert.Equal("0 files, 0 B", DisplayFormat.FilesAndSize(0, 0));
    }

    [Fact]
    public void Fingerprint_preview_is_first_twelve_hex_chars_after_prefix()
    {
        Assert.Equal("3f9a1c0b7e21", DisplayFormat.FingerprintPreview("sha256:3f9a1c0b7e21deadbeef00"));
    }

    [Fact]
    public void Fingerprint_preview_of_short_value_is_returned_whole()
    {
        Assert.Equal("abc", DisplayFormat.FingerprintPreview("sha256:abc"));
    }

    [Fact]
    public void Fingerprint_preview_without_prefix_takes_first_twelve_chars()
    {
        Assert.Equal("0123456789ab", DisplayFormat.FingerprintPreview("0123456789abcdef"));
    }

    [Fact]
    public void No_last_sync_reads_never_synced()
    {
        Assert.Equal("Never synced", DisplayFormat.LastSyncText(null));
    }

    [Fact]
    public void Last_sync_from_switch_reads_with_ascii_arrow()
    {
        var text = DisplayFormat.LastSyncText(new LastSync("sha256:a", LocalTime, SyncDirection.SwitchToPc));

        Assert.Equal("Last sync: 2026-11-03 14:25, Switch -> PC", text);
    }

    [Fact]
    public void Last_sync_to_switch_reads_with_reversed_arrow()
    {
        var text = DisplayFormat.LastSyncText(new LastSync("sha256:a", LocalTime, SyncDirection.PcToSwitch));

        Assert.Equal("Last sync: 2026-11-03 14:25, PC -> Switch", text);
    }
}
