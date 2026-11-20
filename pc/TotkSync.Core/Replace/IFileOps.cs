namespace TotkSync.Core.Replace;

public interface IFileOps
{
    void CreateDirectory(string path);

    void WriteFile(string path, ReadOnlySpan<byte> content);

    void CopyFile(string from, string to);

    void MoveDirectory(string from, string to);

    void MoveFile(string from, string to, bool overwrite);

    void DeleteDirectory(string path);

    void DeleteFile(string path);
}
