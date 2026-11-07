using System.Net;
using Microsoft.Extensions.DependencyInjection;
using TotkSync.App.Pages.Activity;
using TotkSync.App.Pages.Backups;
using TotkSync.App.Pages.Home;
using TotkSync.App.Pages.Settings;
using TotkSync.App.Services;
using TotkSync.Core.Activity;
using TotkSync.Core.Game;
using TotkSync.Core.Settings;
using TotkSync.Core.Pairing;
using TotkSync.Core.Status;
using TotkSync.Server;
using TotkSync.Core.Sync;

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
        services.AddSingleton(provider => new ServerHost(
            provider.GetRequiredService<SettingsSession>(),
            provider.GetRequiredService<StatusQuery>(),
            provider.GetRequiredService<ActivityLog>(),
            provider.GetRequiredService<TimeProvider>(),
            IPAddress.Any,
            LanAddresses.Detect));
    }

    private static void AddUiServices(IServiceCollection services)
    {
        services.AddSingleton<FolderPicker>();
        services.AddSingleton<ShellOpener>();
        services.AddSingleton<Navigator>();
        services.AddSingleton<UiThread>();
        services.AddSingleton<MainWindow>();
    }

    private static void AddPages(IServiceCollection services)
    {
        services.AddSingleton<ServerCardViewModel>();
        services.AddSingleton<HomeViewModel>();
        services.AddSingleton<HomePage>();
        services.AddSingleton<SettingsViewModel>();
        services.AddSingleton<SettingsPage>();
        services.AddSingleton<BackupsPage>();
        services.AddSingleton<ActivityViewModel>();
        services.AddSingleton<ActivityPage>();
    }
}
