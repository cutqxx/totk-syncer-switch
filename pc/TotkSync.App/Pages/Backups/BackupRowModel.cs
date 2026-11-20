namespace TotkSync.App.Pages.Backups;

public sealed record BackupRowModel(string DateText, string ReasonText, string SizeText, string FileName, string AutomationName)
{
    /// <summary>A screen reader names a list row by its item's text, so the row reads as <see cref="AutomationName"/>.</summary>
    public override string ToString() => AutomationName;
}
