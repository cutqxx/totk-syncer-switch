using System.Diagnostics;
using TotkSync.Core.Settings;

namespace TotkSync.Core.Game;

public sealed class GameProcess : IGameProcess
{
    private static readonly string ProcessName = Path.GetFileNameWithoutExtension(SettingsValidator.GameExecutable);

    public bool IsRunning()
    {
        var processes = Process.GetProcessesByName(ProcessName);
        foreach (var process in processes)
        {
            process.Dispose();
        }
        return processes.Length > 0;
    }
}
