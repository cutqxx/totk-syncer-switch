using System.ComponentModel;
using System.Windows;
using TotkSync.App.Dialogs;
using TotkSync.App.Pages.Home;
using TotkSync.App.Services;
using TotkSync.Core.Sync;
using Wpf.Ui;
using Wpf.Ui.Animations;
using Wpf.Ui.Appearance;
using Wpf.Ui.Controls;

namespace TotkSync.App;

public partial class MainWindow : FluentWindow
{
    private readonly LauncherShutdown _shutdown;
    private readonly IContentDialogService _dialogs;
    private bool _isClosing;
    private bool _isServerStopped;

    public MainWindow(
        IServiceProvider services,
        Navigator navigator,
        LauncherShutdown shutdown,
        IContentDialogService dialogs,
        ISnackbarService snackbar)
    {
        _shutdown = shutdown;
        _dialogs = dialogs;
        InitializeComponent();
        SystemThemeWatcher.Watch(this);
        dialogs.SetDialogHost(RootContentDialog);
        snackbar.SetSnackbarPresenter(SnackbarHost);
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
        await ShutdownShowingProgressAsync();
        _isServerStopped = true;
        Close();
    }

    private async Task ShutdownShowingProgressAsync()
    {
        FinishingTransferDialog? dialog = null;
        await _shutdown.ShutdownAsync(() =>
        {
            dialog = new FinishingTransferDialog(_dialogs.GetDialogHostEx());
            _ = dialog.ShowAsync();
        });
        dialog?.Hide();
    }

    private void DisableTransitionsWhenWindowsAnimationsAreOff()
    {
        if (!SystemParameters.ClientAreaAnimation)
        {
            Navigation.Transition = Transition.None;
        }
    }
}
