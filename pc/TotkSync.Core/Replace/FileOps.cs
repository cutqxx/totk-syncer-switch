namespace TotkSync.Core.Replace;

public sealed class FileOps : IFileOps
{
    public void CreateDirectory(string path) => Directory.CreateDirectory(path);

    public void WriteFile(string path, ReadOnlySpan<byte> content)
    {
        using var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write);
        stream.Write(content);
        stream.Flush(flushToDisk: true);
    }

    public void CopyFile(string from, string to) => File.Copy(from, to, overwrite: false);

    public void MoveDirectory(string from, string to) => Directory.Move(from, to);

    public void MoveFile(string from, string to, bool overwrite) => File.Move(from, to, overwrite);

    public void DeleteDirectory(string path)
    {
        if (!Directory.Exists(path))
        {
            return;
        }
        ClearReadOnly(path);
        Directory.Delete(path, recursive: true);
    }

    public void DeleteFile(string path)
    {
        if (File.Exists(path))
        {
            File.SetAttributes(path, FileAttributes.Normal);
        }
        File.Delete(path);
    }

    private static void ClearReadOnly(string directory)
    {
        foreach (var entry in new DirectoryInfo(directory).EnumerateFileSystemInfos("*", SearchOption.AllDirectories))
        {
            if (entry.Attributes.HasFlag(FileAttributes.ReadOnly))
            {
                entry.Attributes &= ~FileAttributes.ReadOnly;
            }
        }
    }
}
