using System.IO;
using System.Text.Json;
using System.Windows;
using Microsoft.Extensions.DependencyInjection;
using Wpf.Ui.Appearance;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;

namespace TotkSync.App;

public partial class App : Application
{
    private ServiceProvider? _services;

    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        ApplicationThemeManager.ApplySystemTheme();
        var paths = AppDataPaths.ForCurrentUser();
        var settings = await TryLoadSettingsAsync(paths);
        if (settings is null)
        {
            Shutdown(1);
            return;
        }
        _services = Composition.Build(paths, settings);
        _services.GetRequiredService<MainWindow>().Show();
        await _services.GetRequiredService<SaveStatusCache>().RefreshAsync();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _services?.Dispose();
        base.OnExit(e);
    }

    private static async Task<LauncherSettings?> TryLoadSettingsAsync(AppDataPaths paths)
    {
        try
        {
            return await Task.Run(() => new SettingsStore(paths.SettingsFile).Load());
        }
        catch (Exception error) when (error is IOException or JsonException or UnauthorizedAccessException)
        {
            MessageBox.Show(
                $"Couldn't read {paths.SettingsFile}.\n{error.Message}\nFix or delete the file and start TotK Save Sync again.",
                "TotK Save Sync",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            return null;
        }
    }
}
