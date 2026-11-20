using System.IO;
using System.Windows;
using TotkSync.Core.Settings;

namespace TotkSync.App.Services;

public static class UnhandledErrorReporter
{
    public static void Report(AppDataPaths paths, Exception error)
    {
        AppendToLog(paths.ErrorLogFile, error);
        MessageBox.Show(
            $"Something went wrong, but TotK Save Sync is still running.\n{error.Message}\nDetails: {paths.ErrorLogFile}",
            "TotK Save Sync",
            MessageBoxButton.OK,
            MessageBoxImage.Warning);
    }

    private static void AppendToLog(string logPath, Exception error)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
            File.AppendAllText(logPath, $"{DateTimeOffset.Now:O}\n{error}\n\n");
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException)
        {
            // The message box below still tells the user; there is nowhere else to log to.
        }
    }
}
