namespace TotkSync.Core.Saves;

public static class JunkFilter
{
    public const string MetaFileName = ".nx_save_meta.bin";

    private static readonly string[] JunkNames = [MetaFileName, "desktop.ini", "thumbs.db"];
    private static readonly string[] JunkExtensions = [".7z", ".zip", ".rar"];

    public static bool IsMeta(string relPath) => IsName(FileName(relPath), MetaFileName);

    public static bool IsJunk(string relPath)
    {
        var name = FileName(relPath);
        return JunkNames.Any(junk => IsName(name, junk))
            || JunkExtensions.Any(ext => name.EndsWith(ext, StringComparison.OrdinalIgnoreCase));
    }

    private static string FileName(string relPath)
    {
        return relPath[(relPath.LastIndexOfAny(['/', '\\']) + 1)..];
    }

    private static bool IsName(string name, string expected)
    {
        return string.Equals(name, expected, StringComparison.OrdinalIgnoreCase);
    }
}
