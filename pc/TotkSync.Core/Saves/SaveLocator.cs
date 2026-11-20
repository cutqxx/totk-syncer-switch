using TotkSync.Core.Replace;

namespace TotkSync.Core.Saves;

public sealed record SaveCandidate(string Dir, DateTimeOffset Modified);

public abstract record LocateResult
{
    private LocateResult()
    {
    }

    public sealed record Found(string Dir) : LocateResult;

    public sealed record NotFound : LocateResult;

    public sealed record Several(IReadOnlyList<SaveCandidate> Items) : LocateResult;
}

public static class SaveLocator
{
    public const string NotFoundMessage =
        "PC save not found: start TotK on PC once or set save folder in Settings";

    public static LocateResult Locate(string gameDir, string? saveDirOverride)
    {
        if (!string.IsNullOrWhiteSpace(saveDirOverride))
        {
            return Directory.Exists(saveDirOverride)
                ? new LocateResult.Found(saveDirOverride)
                : new LocateResult.NotFound();
        }
        return FromCandidates(FindCandidates(gameDir));
    }

    private static LocateResult FromCandidates(IReadOnlyList<SaveCandidate> candidates)
    {
        return candidates.Count switch
        {
            0 => new LocateResult.NotFound(),
            1 => new LocateResult.Found(candidates[0].Dir),
            _ => new LocateResult.Several(candidates),
        };
    }

    public static string? ParentDirectory(string? gameDir, string? saveDirOverride)
    {
        if (!string.IsNullOrWhiteSpace(saveDirOverride))
        {
            return Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(saveDirOverride));
        }
        return string.IsNullOrWhiteSpace(gameDir) ? null : ProfileDirectory(gameDir);
    }

    private static string ProfileDirectory(string gameDir) => Path.Combine(gameDir, "profile");

    private static IReadOnlyList<SaveCandidate> FindCandidates(string gameDir)
    {
        var profileDir = ProfileDirectory(gameDir);
        if (!Directory.Exists(profileDir))
        {
            return [];
        }
        return Directory.EnumerateDirectories(profileDir)
            .Where(dir => !StagingNames.IsLeftover(dir))
            .Where(SaveLayout.IsSaveDirectory)
            .Order(StringComparer.OrdinalIgnoreCase)
            .Select(ToCandidate)
            .ToList();
    }

    private static SaveCandidate ToCandidate(string dir)
    {
        return new SaveCandidate(dir, SaveFile.LatestModified(SaveFile.Enumerate(dir), dir));
    }
}
