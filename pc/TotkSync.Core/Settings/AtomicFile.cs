namespace TotkSync.Core.Settings;

internal static class AtomicFile
{
    public static void WriteAllBytes(string path, byte[] content)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var tempPath = path + ".tmp";
        try
        {
            WriteTemp(tempPath, content);
            File.Move(tempPath, path, overwrite: true);
        }
        catch
        {
            TryDelete(tempPath);
            throw;
        }
    }

    private static void WriteTemp(string tempPath, byte[] content)
    {
        using var stream = new FileStream(tempPath, FileMode.Create, FileAccess.Write);
        stream.Write(content);
        stream.Flush(flushToDisk: true);
    }

    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
        }
    }
}
