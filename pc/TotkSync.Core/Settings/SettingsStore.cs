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
        var settings = File.Exists(path)
            ? JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllBytes(path), Options) ?? new LauncherSettings()
            : new LauncherSettings();
        ThrowIfInvalid(settings);
        return settings;
    }

    public LauncherSettings EnsureToken(LauncherSettings settings)
    {
        if (!string.IsNullOrWhiteSpace(settings.Token))
        {
            return settings;
        }
        var withToken = settings with { Token = TokenGenerator.Generate() };
        Save(withToken);
        return withToken;
    }

    public void Save(LauncherSettings settings)
    {
        AtomicFile.WriteAllBytes(path, JsonSerializer.SerializeToUtf8Bytes(settings, Options));
    }

    private void ThrowIfInvalid(LauncherSettings settings)
    {
        var error = SettingsValidator.PortError(settings.Port) ?? SettingsValidator.KeepBackupsError(settings.KeepBackups);
        if (error is not null)
        {
            throw new InvalidSettingsException($"Invalid value in {path}: {error}");
        }
    }
}
