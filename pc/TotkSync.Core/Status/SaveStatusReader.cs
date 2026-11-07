using System.Text.Json;
using TotkSync.Core.Saves;
using TotkSync.Core.Settings;
using TotkSync.Core.Sync;

namespace TotkSync.Core.Status;

public sealed class SaveStatusReader(SyncStateStore syncState)
{
    public SaveStatus Read(LauncherSettings settings)
    {
        if (string.IsNullOrWhiteSpace(settings.GameDir) || SettingsValidator.GameDirError(settings.GameDir) is not null)
        {
            return new SaveStatus.NotConfigured();
        }
        try
        {
            return ReadConfigured(settings.GameDir, settings.SaveDir);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
        {
            return new SaveStatus.Unreadable(error.Message);
        }
    }

    private SaveStatus ReadConfigured(string gameDir, string? saveDirOverride)
    {
        return SaveLocator.Locate(gameDir, saveDirOverride) switch
        {
            LocateResult.Found found => new SaveStatus.Found(SaveSnapshot.Capture(found.Dir), syncState.Load()),
            LocateResult.Several several => new SaveStatus.Several(several.Items),
            _ => new SaveStatus.NotFound(),
        };
    }
}
