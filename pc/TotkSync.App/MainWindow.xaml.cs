using System.Windows;
using TotkSync.App.Pages.Home;
using TotkSync.App.Services;
using Wpf.Ui.Animations;
using Wpf.Ui.Appearance;
using Wpf.Ui.Controls;

namespace TotkSync.App;

public partial class MainWindow : FluentWindow
{
    public MainWindow(IServiceProvider services, Navigator navigator)
    {
        InitializeComponent();
        SystemThemeWatcher.Watch(this);
        Navigation.SetServiceProvider(services);
        navigator.Attach(pageType => Navigation.Navigate(pageType));
        DisableTransitionsWhenWindowsAnimationsAreOff();
        Loaded += (_, _) => Navigation.Navigate(typeof(HomePage));
    }

    private void DisableTransitionsWhenWindowsAnimationsAreOff()
    {
        if (!SystemParameters.ClientAreaAnimation)
        {
            Navigation.Transition = Transition.None;
        }
    }
}
