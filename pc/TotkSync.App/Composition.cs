using System.Diagnostics;
using System.Net;
using Microsoft.Extensions.DependencyInjection;
using TotkSync.App.Pages.Activity;
using TotkSync.App.Pages.Backups;
using TotkSync.App.Pages.Home;
using TotkSync.App.Pages.Settings;
using TotkSync.App.Services;
using TotkSync.Core.Activity;
using TotkSync.Core.Backups;
using TotkSync.Core.Game;
using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Settings;
using TotkSync.Core.Pairing;
using TotkSync.Core.Status;
using TotkSync.Server;
using TotkSync.Core.Sync;
using Wpf.Ui;

namespace TotkSync.App;

public static class Composition
{
    public static ServiceProvider Build(AppDataPaths paths, LauncherSettings settings)
    {
        var services = new ServiceCollection();
        AddCore(services, paths, settings);
        AddUiServices(services);
        AddPages(services);
        return services.BuildServiceProvider();
    }

    private static void AddCore(IServiceCollection services, AppDataPaths paths, LauncherSettings settings)
    {
        services.AddSingleton(paths);
        services.AddSingleton(new SettingsStore(paths.SettingsFile));
        services.AddSingleton(provider => new SettingsSession(provider.GetRequiredService<SettingsStore>(), settings));
        services.AddSingleton(new SyncStateStore(paths.SyncStateFile));
        services.AddSingleton<SaveStatusReader>();
        services.AddSingleton<SaveStatusCache>();
        services.AddSingleton(TimeProvider.System);
        services.AddSingleton<IGameProcess, GameProcess>();
        services.AddSingleton<StatusQuery>();
        services.AddSingleton(new ActivityLog(paths.ActivityFile));
        AddGame(services);
        AddSaveReceiving(services, paths);
        services.AddSingleton(provider => new ServerHost(
            provider.GetRequiredService<SettingsSession>(),
            provider.GetRequiredService<StatusQuery>(),
            provider.GetRequiredService<ActivityLog>(),
            provider.GetRequiredService<TimeProvider>(),
            IPAddress.Any,
            LanAddresses.Detect,
            provider.GetRequiredService<SaveReceiver>(),
            error => RecoveryLog.Append(provider.GetRequiredService<AppDataPaths>().ErrorLogFile, [error.ToString()], "Server error"),
            provider.GetRequiredService<SaveSender>()));
    }

    private static void AddGame(IServiceCollection services)
    {
        services.AddSingleton(provider => new GameWatcher(
            provider.GetRequiredService<IGameProcess>(),
            provider.GetRequiredService<TransferGate>(),
            provider.GetRequiredService<SaveStatusCache>(),
            provider.GetRequiredService<TimeProvider>(),
            error => RecoveryLog.Append(provider.GetRequiredService<AppDataPaths>().ErrorLogFile, [error.ToString()], "Game watcher error"),
            provider.GetRequiredService<PendingRecovery>()));
        services.AddSingleton(provider => new GameLauncher(
            provider.GetRequiredService<IGameProcess>(),
            provider.GetRequiredService<TransferGate>(),
            info => Process.Start(info)?.Dispose(),
            provider.GetRequiredService<PendingRecovery>()));
        services.AddSingleton(provider => new LauncherShutdown(
            provider.GetRequiredService<TransferGate>(),
            provider.GetRequiredService<ServerHost>().StopAsync,
            provider.GetRequiredService<GameLauncher>().CancelPendingAndRefuseNew));
    }

    private static void AddSaveReceiving(IServiceCollection services, AppDataPaths paths)
    {
        services.AddSingleton<IFileOps, FileOps>();
        services.AddSingleton<TransferGate>();
        services.AddSingleton(provider => new BackupStore(
            paths.BackupsDir,
            provider.GetRequiredService<IFileOps>(),
            provider.GetRequiredService<TimeProvider>()));
        services.AddSingleton<MirrorReplacer>();
        services.AddSingleton<LeftoverRecovery>();
        services.AddSingleton(provider => new PendingRecovery(
            provider.GetRequiredService<IGameProcess>(),
            provider.GetRequiredService<TransferGate>(),
            () => RecoverLeftovers(provider),
            notes => RecoveryLog.Append(provider.GetRequiredService<AppDataPaths>().ErrorLogFile, notes)));
        services.AddSingleton<SaveReceiver>();
        services.AddSingleton<SaveSender>();
    }

    private static IReadOnlyList<string> RecoverLeftovers(IServiceProvider provider)
    {
        try
        {
            var settings = provider.GetRequiredService<SettingsSession>().Current;
            var parentDir = SaveLocator.ParentDirectory(settings.GameDir, settings.SaveDir);
            provider.GetRequiredService<BackupStore>().DeleteOrphanedParts();
            return parentDir is null ? [] : provider.GetRequiredService<LeftoverRecovery>().Recover(parentDir);
        }
        catch (Exception failure)
        {
            return [$"Startup recovery failed: {failure.Message}"];
        }
    }

    private static void AddUiServices(IServiceCollection services)
    {
        services.AddSingleton<FolderPicker>();
        services.AddSingleton<ShellOpener>();
        services.AddSingleton<Navigator>();
        services.AddSingleton<UiThread>();
        services.AddSingleton<ClipboardService>();
        services.AddSingleton<SaveFilePicker>();
        services.AddSingleton<Toast>();
        services.AddSingleton<FirewallHelpPresenter>();
        services.AddSingleton<IContentDialogService, ContentDialogService>();
        services.AddSingleton<ISnackbarService, SnackbarService>();
        services.AddSingleton<MainWindow>();
    }

    private static void AddPages(IServiceCollection services)
    {
        services.AddSingleton<PairingViewModel>();
        services.AddSingleton<ServerCardViewModel>();
        services.AddSingleton<PlayCardViewModel>();
        services.AddSingleton<HomeViewModel>();
        services.AddSingleton<HomePage>();
        services.AddSingleton<SettingsViewModel>();
        services.AddSingleton<SettingsPage>();
        services.AddSingleton<BackupsViewModel>();
        services.AddSingleton<BackupsPage>();
        services.AddSingleton<ActivityViewModel>();
        services.AddSingleton<ActivityPage>();
    }
}
