using TotkSync.Core.Backups;
using TotkSync.Core.Display;

namespace TotkSync.App.Pages.Backups;

public static class BackupPresenter
{
    public static BackupRowModel ToRow(BackupEntry entry)
    {
        var dateText = DisplayFormat.Date(entry.LocalTime);
        var reasonText = ReasonText(entry.Kind);
        var sizeText = DisplayFormat.Size(entry.Bytes);
        return new BackupRowModel(dateText, reasonText, sizeText, entry.Name, $"{dateText}, {reasonText}, {sizeText}");
    }

    public static string KeepingText(int keepBackups)
    {
        return $"Keeping the {keepBackups} newest backups. Change this in Settings.";
    }

    private static string ReasonText(string kind)
    {
        return kind switch
        {
            BackupName.BeforeSwitchKind => "Before save from Switch",
            BackupName.RecoveredKind => "Recovered after a failed save",
            _ => kind,
        };
    }
}
