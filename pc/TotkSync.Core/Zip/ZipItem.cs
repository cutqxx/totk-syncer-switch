using System.IO.Compression;

namespace TotkSync.Core.Zip;

internal sealed record ZipItem(ZipArchiveEntry Entry, string Path, bool IsDirectory)
{
    public static ZipItem From(ZipArchiveEntry entry)
    {
        var isDirectory = entry.FullName.EndsWith('/');
        return new ZipItem(entry, isDirectory ? entry.FullName[..^1] : entry.FullName, isDirectory);
    }
}
