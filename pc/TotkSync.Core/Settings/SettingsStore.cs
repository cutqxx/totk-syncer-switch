using System.Text.Json;

namespace TotkSync.Core.Settings;

public sealed class SettingsStore(string path)
{
    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        WriteIndented = true,
    };

    public LauncherSettings Load()
    {
        return File.Exists(path)
            ? JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllBytes(path), Options) ?? new LauncherSettings()
            : new LauncherSettings();
    }

    public void Save(LauncherSettings settings)
    {
        AtomicFile.WriteAllBytes(path, JsonSerializer.SerializeToUtf8Bytes(settings, Options));
    }
}
