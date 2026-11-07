namespace TotkSync.Core.Settings;

public sealed class SettingsSession(SettingsStore store, LauncherSettings initial)
{
    private readonly SemaphoreSlim _gate = new(1, 1);

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
        var error = await ApplySerializedAsync(validate, change);
        if (error is null)
        {
            Changed?.Invoke();
        }
        return error;
    }

    private async Task<string?> ApplySerializedAsync(Func<string?> validate, Func<LauncherSettings, LauncherSettings> change)
    {
        await _gate.WaitAsync();
        try
        {
            return await ValidateAndSaveAsync(validate, change);
        }
        finally
        {
            _gate.Release();
        }
    }

    private async Task<string?> ValidateAndSaveAsync(Func<string?> validate, Func<LauncherSettings, LauncherSettings> change)
    {
        var error = await Task.Run(validate);
        if (error is not null)
        {
            return error;
        }
        var updated = change(Current);
        try
        {
            await Task.Run(() => store.Save(updated));
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException)
        {
            return $"Couldn't save settings: {failure.Message}";
        }
        Current = updated;
        return null;
    }
}
