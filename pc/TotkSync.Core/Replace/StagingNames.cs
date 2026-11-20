using System.Text.RegularExpressions;

namespace TotkSync.Core.Replace;

public sealed partial record StagingNames(string Stage, string Old)
{
    private const string NewKind = "new";
    private const string OldKind = "old";
    private const string BadKind = "bad";
    private const string UnverifiedSuffix = ".unverified";
    private const int SuffixLength = 8;

    public static StagingNames For(string saveDir)
    {
        var baseDir = Path.TrimEndingDirectorySeparator(saveDir);
        var suffix = NewSuffix();
        return new StagingNames(NameOf(baseDir, NewKind, suffix), NameOf(baseDir, OldKind, suffix));
    }

    public static string BadFor(string saveDir) => NameOf(Path.TrimEndingDirectorySeparator(saveDir), BadKind, NewSuffix());

    public static string UnverifiedMarkerFor(string oldPath) => oldPath + UnverifiedSuffix;

    public static bool IsLeftover(string path) => Parse(path).Success;

    public static bool IsOld(string path)
    {
        var match = Parse(path);
        return match.Success && match.Groups["kind"].Value == OldKind;
    }

    public static bool IsBad(string path)
    {
        var match = Parse(path);
        return match.Success && match.Groups["kind"].Value == BadKind;
    }

    public static bool IsUnverifiedMarker(string path) => MarkerPattern().IsMatch(Path.GetFileName(path));

    public static string OldOfMarker(string markerPath) => markerPath[..^UnverifiedSuffix.Length];

    public static string OriginalOf(string leftoverPath)
    {
        var match = Parse(leftoverPath);
        if (!match.Success)
        {
            throw new ArgumentException($"Not a staging folder name: {leftoverPath}", nameof(leftoverPath));
        }
        return Path.Combine(Path.GetDirectoryName(leftoverPath)!, match.Groups["original"].Value);
    }

    private static string NewSuffix() => Guid.NewGuid().ToString("N")[..SuffixLength];

    private static string NameOf(string baseDir, string kind, string suffix) => $"{baseDir}.totksync-{kind}-{suffix}";

    private static Match Parse(string path) => LeftoverPattern().Match(Path.GetFileName(Path.TrimEndingDirectorySeparator(path)));

    [GeneratedRegex(@"^(?<original>.+)\.totksync-(?<kind>new|old|bad)-(?<id>[0-9a-f]{8})$")]
    private static partial Regex LeftoverPattern();

    [GeneratedRegex(@"^.+\.totksync-old-[0-9a-f]{8}\.unverified$")]
    private static partial Regex MarkerPattern();
}
