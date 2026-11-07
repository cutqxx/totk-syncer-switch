namespace TotkSync.Core.Settings;

internal static class AtomicFile
{
    public static void WriteAllBytes(string path, byte[] content)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var tempPath = path + ".tmp";
        using (var stream = new FileStream(tempPath, FileMode.Create, FileAccess.Write))
        {
            stream.Write(content);
            stream.Flush(flushToDisk: true);
        }
        File.Move(tempPath, path, overwrite: true);
    }
}
