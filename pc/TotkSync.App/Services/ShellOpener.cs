using System.Diagnostics;
using System.IO;

namespace TotkSync.App.Services;

public sealed class ShellOpener
{
    public Task OpenFolderAsync(string dir)
    {
        return Task.Run(() =>
        {
            Directory.CreateDirectory(dir);
            Process.Start(new ProcessStartInfo(dir) { UseShellExecute = true })?.Dispose();
        });
    }
}
