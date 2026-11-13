using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Threading;
using Microsoft.Extensions.DependencyInjection;
using TotkSync.App.Pages.Activity;
using TotkSync.App.Services;
using TotkSync.Core.Backups;
using TotkSync.Core.Game;
using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Server;
using Wpf.Ui.Appearance;

namespace TotkSync.App;

public partial class App : Application
{
    private ServiceProvider? _services;

    public App()
    {
        DispatcherUnhandledException += OnDispatcherUnhandledException;
    }

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
        await StartAsync(_services);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _services?.Dispose();
        base.OnExit(e);
    }

    private static async Task StartAsync(IServiceProvider services)
    {
        await RecoverInterruptedReplaceAsync(services);
        var activityLoad = services.GetRequiredService<ActivityViewModel>().InitializeAsync();
        services.GetRequiredService<MainWindow>().Show();
        await services.GetRequiredService<SaveStatusCache>().RefreshAsync();
        await services.GetRequiredService<ServerHost>().StartAsync();
        _ = services.GetRequiredService<GameWatcher>().Start();
        await activityLoad;
    }

    private static Task RecoverInterruptedReplaceAsync(IServiceProvider services)
    {
        var settings = services.GetRequiredService<SettingsSession>().Current;
        var parentDir = SaveLocator.ParentDirectory(settings.GameDir, settings.SaveDir);
        var recovery = services.GetRequiredService<LeftoverRecovery>();
        var backups = services.GetRequiredService<BackupStore>();
        var logPath = AppDataPaths.ForCurrentUser().ErrorLogFile;
        return Task.Run(() => RecoveryLog.Append(logPath, RecoverQuietly(backups, recovery, parentDir)));
    }

    private static IReadOnlyList<string> RecoverQuietly(BackupStore backups, LeftoverRecovery recovery, string? parentDir)
    {
        try
        {
            backups.DeleteOrphanedParts();
            return parentDir is null ? [] : recovery.Recover(parentDir);
        }
        catch (Exception failure)
        {
            return [$"Startup recovery failed: {failure.Message}"];
        }
    }

    private static void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        UnhandledErrorReporter.Report(AppDataPaths.ForCurrentUser(), e.Exception);
        e.Handled = true;
    }

    private static async Task<LauncherSettings?> TryLoadSettingsAsync(AppDataPaths paths)
    {
        try
        {
            return await Task.Run(() => LoadSettingsWithToken(paths));
        }
        catch (Exception error) when (error is IOException or JsonException or UnauthorizedAccessException or InvalidSettingsException)
        {
            MessageBox.Show(
                $"Couldn't use {paths.SettingsFile}.\n{error.Message}\nFix or delete the file and start TotK Save Sync again.",
                "TotK Save Sync",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            return null;
        }
    }

    private static LauncherSettings LoadSettingsWithToken(AppDataPaths paths)
    {
        var store = new SettingsStore(paths.SettingsFile);
        return store.EnsureToken(store.Load());
    }
}
