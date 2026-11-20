namespace TotkSync.Core.Settings;

public sealed class AppDataPaths(string root)
{
    public string Root { get; } = root;

    public string SettingsFile => Path.Combine(Root, "settings.json");

    public string SyncStateFile => Path.Combine(Root, "sync_state", "state.json");

    public string ActivityFile => Path.Combine(Root, "activity.jsonl");

    public string ErrorLogFile => Path.Combine(Root, "error.log");

    public string BackupsDir => Path.Combine(Root, "backups");

    public static AppDataPaths ForCurrentUser()
    {
        var localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return new AppDataPaths(Path.Combine(localAppData, "TotkSync"));
    }
}
