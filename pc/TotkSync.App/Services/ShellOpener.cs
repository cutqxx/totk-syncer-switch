using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using TotkSync.Core.Settings;

namespace TotkSync.App.Services;

public sealed class ShellOpener
{
    public Task<string?> OpenFolderAsync(string dir)
    {
        return Task.Run(() => OpenExisting(dir));
    }

    public Task<string?> OpenAppDataFolderAsync(AppDataPaths paths)
    {
        return Task.Run(() =>
        {
            try
            {
                Directory.CreateDirectory(paths.Root);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                return $"Couldn't create {paths.Root}: {error.Message}";
            }
            return OpenExisting(paths.Root);
        });
    }

    private static string? OpenExisting(string dir)
    {
        if (!Directory.Exists(dir))
        {
            return $"Folder not found: {dir}";
        }
        try
        {
            Process.Start(new ProcessStartInfo(dir) { UseShellExecute = true })?.Dispose();
            return null;
        }
        catch (Win32Exception error)
        {
            return $"Couldn't open {dir}: {error.Message}";
        }
    }
}
