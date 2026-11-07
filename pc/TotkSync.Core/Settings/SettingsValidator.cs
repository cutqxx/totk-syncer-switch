namespace TotkSync.Core.Settings;

public static class SettingsValidator
{
    public const int MinPort = 1024;
    public const int MaxPort = 65535;
    public const string GameExecutable = "totk.exe";

    public static string? GameDirError(string path)
    {
        return File.Exists(Path.Combine(path, GameExecutable))
            ? null
            : $"{GameExecutable} not found in {path}";
    }

    public static string? PortError(int port)
    {
        return port is >= MinPort and <= MaxPort
            ? null
            : $"Port must be between {MinPort} and {MaxPort}.";
    }

    public static string? KeepBackupsError(int keepBackups)
    {
        return keepBackups >= 1 ? null : "Keep at least 1 backup.";
    }
}
