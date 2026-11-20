using System.IO;
using System.Windows.Threading;
using Microsoft.Extensions.DependencyInjection;
using TotkSync.Core.Settings;

namespace TotkSync.App.Tests;

/// <summary>
/// One WPF <see cref="App"/> per test process (WPF allows only one Application per AppDomain), running on its
/// own STA thread with the real merged resource dictionaries. Nothing is started: no server, no game watcher.
/// </summary>
internal static class WpfApp
{
    private static readonly Lazy<(Dispatcher Dispatcher, ServiceProvider Services)> Instance = new(Start);

    public static ServiceProvider Services => Instance.Value.Services;

    public static void Run(Action action)
    {
        Exception? failure = null;
        Instance.Value.Dispatcher.Invoke(() =>
        {
            try
            {
                action();
            }
            catch (Exception e)
            {
                failure = e;
            }
        });
        if (failure is not null)
        {
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
        }
    }

    private static (Dispatcher, ServiceProvider) Start()
    {
        var ready = new ManualResetEventSlim();
        Dispatcher? dispatcher = null;
        ServiceProvider? services = null;
        Exception? failure = null;
        var thread = new Thread(() =>
        {
            try
            {
                var app = new App { ShutdownMode = System.Windows.ShutdownMode.OnExplicitShutdown };
                app.InitializeComponent();
                var root = Path.Combine(Path.GetTempPath(), "totksync-ui-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(root);
                var gameDir = Directory.CreateDirectory(Path.Combine(root, "game")).FullName;
                services = Composition.Build(new AppDataPaths(Path.Combine(root, "appdata")), new LauncherSettings(GameDir: gameDir));
                dispatcher = Dispatcher.CurrentDispatcher;
            }
            catch (Exception e)
            {
                failure = e;
            }
            ready.Set();
            if (failure is null)
            {
                Dispatcher.Run();
            }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.IsBackground = true;
        thread.Start();
        ready.Wait();
        if (failure is not null)
        {
            throw new InvalidOperationException("The WPF app could not start for UI tests.", failure);
        }
        return (dispatcher!, services!);
    }
}
