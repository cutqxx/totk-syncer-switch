using System.ComponentModel;
using System.Windows;
using TotkSync.App.Pages.Home;
using TotkSync.App.Services;
using TotkSync.Server;
using Wpf.Ui.Animations;
using Wpf.Ui.Appearance;
using Wpf.Ui.Controls;

namespace TotkSync.App;

public partial class MainWindow : FluentWindow
{
    private readonly ServerHost _server;
    private bool _isClosing;
    private bool _isServerStopped;

    public MainWindow(IServiceProvider services, Navigator navigator, ServerHost server)
    {
        _server = server;
        InitializeComponent();
        SystemThemeWatcher.Watch(this);
        Navigation.SetServiceProvider(services);
        navigator.Attach(pageType => Navigation.Navigate(pageType));
        DisableTransitionsWhenWindowsAnimationsAreOff();
        Loaded += (_, _) => Navigation.Navigate(typeof(HomePage));
    }

    protected override async void OnClosing(CancelEventArgs e)
    {
        if (_isServerStopped)
        {
            base.OnClosing(e);
            return;
        }
        e.Cancel = true;
        if (_isClosing)
        {
            return;
        }
        _isClosing = true;
        await _server.StopAsync();
        _isServerStopped = true;
        Close();
    }

    private void DisableTransitionsWhenWindowsAnimationsAreOff()
    {
        if (!SystemParameters.ClientAreaAnimation)
        {
            Navigation.Transition = Transition.None;
        }
    }
}
