namespace TotkSync.App.Dialogs;

public sealed record FirewallHelpModel(string Command, string? CheckUrl)
{
    public bool HasCheckUrl => CheckUrl is not null;

    public string CheckText => $"To check: on a phone on the same Wi-Fi, open {CheckUrl}. \"Unauthorized\" means the connection works.";
}
