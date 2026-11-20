using System.Globalization;
using TotkSync.Core.Sync;

namespace TotkSync.Core.Display;

public static class DisplayFormat
{
    private const int FingerprintPreviewLength = 12;
    private const string FingerprintPrefix = "sha256:";

    public static string Date(DateTimeOffset value)
    {
        return value.ToLocalTime().ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture);
    }

    public static string DateWithSeconds(DateTimeOffset value)
    {
        return value.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture);
    }

    public static string TimeWithSeconds(DateTimeOffset value)
    {
        return value.ToLocalTime().ToString("HH:mm:ss", CultureInfo.InvariantCulture);
    }

    public static string Size(long bytes)
    {
        return bytes switch
        {
            >= 1_000_000 => string.Create(CultureInfo.InvariantCulture, $"{bytes / 1_000_000.0:0.0} MB"),
            >= 1_000 => string.Create(CultureInfo.InvariantCulture, $"{bytes / 1_000.0:0.0} KB"),
            _ => $"{bytes} B",
        };
    }

    public static string FilesAndSize(int files, long bytes)
    {
        return $"{files} {(files == 1 ? "file" : "files")}, {Size(bytes)}";
    }

    public static string FingerprintPreview(string fingerprint)
    {
        var hex = fingerprint.StartsWith(FingerprintPrefix, StringComparison.Ordinal)
            ? fingerprint[FingerprintPrefix.Length..]
            : fingerprint;
        return hex[..Math.Min(FingerprintPreviewLength, hex.Length)];
    }

    public static string LastSyncText(LastSync? lastSync)
    {
        if (lastSync is null)
        {
            return "Never synced";
        }
        var arrow = lastSync.Direction == SyncDirection.SwitchToPc ? "Switch -> PC" : "PC -> Switch";
        return $"Last sync: {Date(lastSync.At)}, {arrow}";
    }
}
