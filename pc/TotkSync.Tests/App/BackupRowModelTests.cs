using TotkSync.App.Pages.Backups;

namespace TotkSync.Tests.App;

public sealed class BackupRowModelTests
{
    [Fact]
    public void A_backup_row_reads_as_its_automation_name()
    {
        // A screen reader names a ListView row by its item's text.
        var row = new BackupRowModel("2026-11-03 14:25", "Before save from Switch", "19.2 MB", "f.zip",
            "2026-11-03 14:25, Before save from Switch, 19.2 MB");

        Assert.Equal("2026-11-03 14:25, Before save from Switch, 19.2 MB", row.ToString());
    }
}
