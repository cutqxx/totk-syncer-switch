using TotkSync.App.Pages.Backups;
using TotkSync.Core.Backups;

namespace TotkSync.Tests.App;

public class BackupPresenterTests
{
    private static BackupEntry Entry(string kind = "before-switch", long bytes = 19_200_000)
    {
        return new BackupEntry($"pc-20261103-142501-{kind}.zip", new DateTime(2026, 11, 3, 14, 25, 1), kind, bytes);
    }

    [Fact]
    public void Row_shows_date_without_seconds_reason_and_size()
    {
        var row = BackupPresenter.ToRow(Entry());

        Assert.Equal(("2026-11-03 14:25", "Before save from Switch", "19.2 MB"), (row.DateText, row.ReasonText, row.SizeText));
    }

    [Fact]
    public void Recovered_backup_is_explained_as_recovery_after_a_failed_save()
    {
        var row = BackupPresenter.ToRow(Entry("recovered"));

        Assert.Equal("Recovered after a failed save", row.ReasonText);
    }

    [Fact]
    public void Unknown_reason_is_shown_as_is_rather_than_hidden()
    {
        var row = BackupPresenter.ToRow(Entry("mystery"));

        Assert.Equal("mystery", row.ReasonText);
    }

    [Theory]
    [InlineData(0, "0 B")]
    [InlineData(1_500, "1.5 KB")]
    [InlineData(2_500_000, "2.5 MB")]
    public void Row_formats_the_size_in_readable_units(long bytes, string expected)
    {
        Assert.Equal(expected, BackupPresenter.ToRow(Entry(bytes: bytes)).SizeText);
    }

    [Fact]
    public void Row_keeps_the_file_name_for_identification()
    {
        Assert.Equal("pc-20261103-142501-before-switch.zip", BackupPresenter.ToRow(Entry()).FileName);
    }

    [Fact]
    public void Screen_reader_name_reads_date_reason_and_size_in_one_phrase()
    {
        var row = BackupPresenter.ToRow(Entry());

        Assert.Equal("2026-11-03 14:25, Before save from Switch, 19.2 MB", row.AutomationName);
    }

    [Theory]
    [InlineData(20)]
    [InlineData(1)]
    [InlineData(7)]
    public void Footer_states_how_many_newest_backups_are_kept(int keep)
    {
        Assert.Equal($"Keeping the {keep} newest backups. Change this in Settings.", BackupPresenter.KeepingText(keep));
    }
}
