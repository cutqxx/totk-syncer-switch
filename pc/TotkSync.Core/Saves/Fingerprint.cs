using System.Security.Cryptography;
using System.Text;

namespace TotkSync.Core.Saves;

public static class Fingerprint
{
    private const string Prefix = "sha256:";

    public static string Compute(IEnumerable<(string RelPath, byte[] Sha256)> files)
    {
        using var outer = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var (relPath, sha256) in SortByUtf8Bytes(files))
        {
            outer.AppendData(Encoding.UTF8.GetBytes($"{relPath}\n{Convert.ToHexStringLower(sha256)}\n"));
        }
        return Prefix + Convert.ToHexStringLower(outer.GetHashAndReset());
    }

    public static string OfFiles(IEnumerable<SaveFile> files)
    {
        return Compute(files.Select(file => (file.RelPath, HashContent(file.FullPath))));
    }

    public static string OfDirectory(string dir) => OfFiles(SaveFile.Enumerate(dir));

    private static IEnumerable<(string RelPath, byte[] Sha256)> SortByUtf8Bytes(
        IEnumerable<(string RelPath, byte[] Sha256)> files)
    {
        return files
            .Select(file => (Entry: file, Key: Encoding.UTF8.GetBytes(file.RelPath)))
            .OrderBy(item => item.Key, ByteOrder.Instance)
            .Select(item => item.Entry);
    }

    private static byte[] HashContent(string path)
    {
        using var stream = File.OpenRead(path);
        return SHA256.HashData(stream);
    }

    private sealed class ByteOrder : IComparer<byte[]>
    {
        public static readonly ByteOrder Instance = new();

        public int Compare(byte[]? left, byte[]? right) => left.AsSpan().SequenceCompareTo(right);
    }
}
