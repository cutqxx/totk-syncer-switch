using System.Security.Cryptography;

namespace TotkSync.Tests.Support;

/// <summary>Every file (with content hash) and every directory under a root, junk included.</summary>
public static class TreeSnapshot
{
    public static SortedDictionary<string, string> Capture(string root)
    {
        var snapshot = new SortedDictionary<string, string>(StringComparer.Ordinal);
        if (!Directory.Exists(root))
        {
            return snapshot;
        }
        foreach (var entry in Directory.EnumerateFileSystemEntries(root, "*", SearchOption.AllDirectories))
        {
            var relPath = Path.GetRelativePath(root, entry).Replace('\\', '/');
            snapshot[relPath] = Directory.Exists(entry) ? "<dir>" : Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(entry)));
        }
        return snapshot;
    }

    public static List<string> Leftovers(string root)
    {
        if (!Directory.Exists(root))
        {
            return [];
        }
        return Directory.EnumerateFileSystemEntries(root, "*", SearchOption.AllDirectories)
            .Where(path => Path.GetFileName(path).Contains(".totksync-") || path.EndsWith(".part"))
            .Select(path => Path.GetRelativePath(root, path))
            .ToList();
    }
}
