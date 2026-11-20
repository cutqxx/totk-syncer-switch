namespace TotkSync.Core.Saves;

public sealed record SaveFile(string RelPath, string FullPath, long Length, DateTime LastWriteUtc)
{
    public static IReadOnlyList<SaveFile> Enumerate(string dir)
    {
        return new DirectoryInfo(dir)
            .EnumerateFiles("*", SearchOption.AllDirectories)
            .Select(info => FromInfo(dir, info))
            .Where(file => !JunkFilter.IsJunk(file.RelPath))
            .ToList();
    }

    public static DateTimeOffset LatestModified(IReadOnlyList<SaveFile> files, string fallbackDir)
    {
        var latest = files.Count == 0
            ? Directory.GetLastWriteTimeUtc(fallbackDir)
            : files.Max(file => file.LastWriteUtc);
        return new DateTimeOffset(latest, TimeSpan.Zero);
    }

    private static SaveFile FromInfo(string dir, FileInfo info)
    {
        var relPath = Path.GetRelativePath(dir, info.FullName).Replace('\\', '/');
        return new SaveFile(relPath, info.FullName, info.Length, info.LastWriteTimeUtc);
    }
}
