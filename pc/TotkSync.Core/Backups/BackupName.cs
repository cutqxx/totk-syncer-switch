using System.Globalization;
using System.Text.RegularExpressions;

namespace TotkSync.Core.Backups;

public sealed record BackupInfo(DateTime LocalTime, string Kind);

public static partial class BackupName
{
    public const string PartSuffix = ".part";

    public const string BeforeSwitchKind = "before-switch";
    public const string RecoveredKind = "recovered";

    public static string BeforeSwitch(DateTimeOffset localTime) => Build(localTime, BeforeSwitchKind);

    public static string FirstFreeBeforeSwitch(DateTimeOffset localTime, Func<string, bool> exists)
    {
        return FirstFree(localTime, BeforeSwitchKind, exists);
    }

    public static string FirstFreeRecovered(DateTimeOffset localTime, Func<string, bool> exists)
    {
        return FirstFree(localTime, RecoveredKind, exists);
    }

    public static BackupInfo? TryParse(string fileName)
    {
        var match = BackupPattern().Match(fileName);
        if (!match.Success)
        {
            return null;
        }
        var stamp = match.Groups["stamp"].Value;
        var isTime = DateTime.TryParseExact(stamp, "yyyyMMdd-HHmmss", CultureInfo.InvariantCulture, DateTimeStyles.None, out var time);
        return isTime ? new BackupInfo(time, match.Groups["kind"].Value) : null;
    }

    public static bool IsBackup(string fileName) => TryParse(fileName) is not null;

    public static bool IsPartial(string fileName)
    {
        return fileName.EndsWith(PartSuffix, StringComparison.Ordinal) && IsBackup(fileName[..^PartSuffix.Length]);
    }

    private static string FirstFree(DateTimeOffset localTime, string kind, Func<string, bool> exists)
    {
        var name = Build(localTime, kind);
        return exists(name) ? FirstFree(localTime.AddSeconds(1), kind, exists) : name;
    }

    private static string Build(DateTimeOffset localTime, string kind)
    {
        return $"pc-{localTime.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture)}-{kind}.zip";
    }

    [GeneratedRegex(@"^pc-(?<stamp>[0-9]{8}-[0-9]{6})-(?<kind>before-switch|recovered)\.zip$")]
    private static partial Regex BackupPattern();
}
