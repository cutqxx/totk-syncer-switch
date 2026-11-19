using TotkSync.Core.Saves;
using TotkSync.Core.Settings;

namespace TotkSync.Core.Sync;

/// <summary>Where the PC save lives for a transfer: a usable save folder, none, or several to choose from.</summary>
public sealed record SaveDirLookup(string? Dir, bool HasSeveral)
{
    public static SaveDirLookup For(LauncherSettings current)
    {
        if (string.IsNullOrWhiteSpace(current.GameDir))
        {
            return new SaveDirLookup(null, false);
        }
        return SaveLocator.Locate(current.GameDir, current.SaveDir) switch
        {
            LocateResult.Found found when SaveLayout.IsSaveDirectory(found.Dir) => new SaveDirLookup(found.Dir, false),
            LocateResult.Several => new SaveDirLookup(null, true),
            _ => new SaveDirLookup(null, false),
        };
    }
}
