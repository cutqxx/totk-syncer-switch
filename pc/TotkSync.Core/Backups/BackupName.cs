using System.Globalization;
using System.Text.RegularExpressions;

namespace TotkSync.Core.Backups;

public static partial class BackupName
{
    public const string PartSuffix = ".part";

    private const string BeforeSwitchKind = "before-switch";
    private const string RecoveredKind = "recovered";

    public static string BeforeSwitch(DateTimeOffset localTime) => Build(localTime, BeforeSwitchKind);

    public static string FirstFreeBeforeSwitch(DateTimeOffset localTime, Func<string, bool> exists)
    {
        return FirstFree(localTime, BeforeSwitchKind, exists);
    }

    public static string FirstFreeRecovered(DateTimeOffset localTime, Func<string, bool> exists)
    {
        return FirstFree(localTime, RecoveredKind, exists);
    }

    public static bool IsBackup(string fileName) => BackupPattern().IsMatch(fileName);

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

    [GeneratedRegex(@"^pc-[0-9]{8}-[0-9]{6}-(before-switch|recovered)\.zip$")]
    private static partial Regex BackupPattern();
}
