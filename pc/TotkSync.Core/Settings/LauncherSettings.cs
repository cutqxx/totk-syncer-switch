namespace TotkSync.Core.Settings;

public sealed record LauncherSettings(
    string? GameDir = null,
    string? SaveDir = null,
    int Port = LauncherSettings.DefaultPort,
    string? Token = null,
    int KeepBackups = LauncherSettings.DefaultKeepBackups)
{
    public const int DefaultPort = 8765;
    public const int DefaultKeepBackups = 20;
}
