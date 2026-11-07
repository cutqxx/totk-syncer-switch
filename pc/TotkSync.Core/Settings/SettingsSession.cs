namespace TotkSync.Core.Settings;

public sealed class SettingsSession(SettingsStore store, LauncherSettings initial)
{
    public LauncherSettings Current { get; private set; } = initial;

    public event Action? Changed;

    public Task<string?> SetGameDirAsync(string path)
    {
        return ApplyAsync(() => SettingsValidator.GameDirError(path), settings => settings with { GameDir = path });
    }

    public Task<string?> SetSaveDirAsync(string? path)
    {
        var saveDir = string.IsNullOrWhiteSpace(path) ? null : path;
        return ApplyAsync(() => null, settings => settings with { SaveDir = saveDir });
    }

    public Task<string?> SetPortAsync(int port)
    {
        return ApplyAsync(() => SettingsValidator.PortError(port), settings => settings with { Port = port });
    }

    public Task<string?> SetKeepBackupsAsync(int keepBackups)
    {
        return ApplyAsync(
            () => SettingsValidator.KeepBackupsError(keepBackups),
            settings => settings with { KeepBackups = keepBackups });
    }

    private async Task<string?> ApplyAsync(Func<string?> validate, Func<LauncherSettings, LauncherSettings> change)
    {
        var error = await Task.Run(validate);
        if (error is not null)
        {
            return error;
        }
        var updated = change(Current);
        await Task.Run(() => store.Save(updated));
        Current = updated;
        Changed?.Invoke();
        return null;
    }
}
