using System.IO.Compression;
using TotkSync.Core.Saves;

namespace TotkSync.Core.Zip;

public static class SaveZipPacker
{
    public static byte[] Pack(string saveDir)
    {
        using var output = new MemoryStream();
        using (var archive = new ZipArchive(output, ZipArchiveMode.Create, leaveOpen: true))
        {
            AddDirectories(archive, saveDir);
            AddFiles(archive, saveDir);
        }
        return output.ToArray();
    }

    private static void AddDirectories(ZipArchive archive, string saveDir)
    {
        var directories = Directory.EnumerateDirectories(saveDir, "*", SearchOption.AllDirectories)
            .Select(dir => Path.GetRelativePath(saveDir, dir).Replace('\\', '/'))
            .Where(relPath => !JunkFilter.IsJunk(relPath))
            .Order(StringComparer.Ordinal);
        foreach (var relPath in directories)
        {
            archive.CreateEntry(relPath + "/");
        }
    }

    private static void AddFiles(ZipArchive archive, string saveDir)
    {
        foreach (var file in SaveFile.Enumerate(saveDir).OrderBy(file => file.RelPath, StringComparer.Ordinal))
        {
            archive.CreateEntryFromFile(file.FullPath, file.RelPath, CompressionLevel.Fastest);
        }
    }
}
