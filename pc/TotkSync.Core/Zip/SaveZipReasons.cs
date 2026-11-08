namespace TotkSync.Core.Zip;

public static class SaveZipReasons
{
    public const string NotAZip = "not a zip file";
    public const string TooManyEntries = "too many entries (max 1000)";
    public const string TooLarge = "unpacked size over 64 MiB";
    public const string SuspiciousCompression = "suspicious compression ratio";
    public const string MissingOption = "missing option.sav";
    public const string MissingProgress = "missing slot_0N/progress.sav";
    public const string AnotherGame = "save of another game";

    private const int MaxPrintedLength = 80;

    public static string UnsafePath(string path) => $"unsafe path: {Printable(path)}";

    public static string SymlinkEntry(string path) => $"symlink entry: {Printable(path)}";

    public static string DuplicatePath(string path) => $"duplicate path: {Printable(path)}";

    public static string ConflictingPaths(string path) => $"conflicting paths: {Printable(path)}";

    public static string BadCrc(string path) => $"bad CRC: {Printable(path)}";

    private static string Printable(string path)
    {
        var shown = path.Length <= MaxPrintedLength ? path : path[..MaxPrintedLength];
        return string.Concat(shown.Select(c => c is >= ' ' and <= '~' ? c : '?'));
    }
}
