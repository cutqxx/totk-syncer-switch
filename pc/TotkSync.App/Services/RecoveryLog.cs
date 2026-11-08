using System.IO;

namespace TotkSync.App.Services;

public static class RecoveryLog
{
    public static void Append(string logPath, IReadOnlyList<string> notes)
    {
        if (notes.Count == 0)
        {
            return;
        }
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
            File.AppendAllText(logPath, $"{DateTimeOffset.Now:O}\nStartup recovery:\n{string.Join('\n', notes)}\n\n");
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException)
        {
            // Recovery itself is done; a missing log line must not stop the launcher from starting.
        }
    }
}
