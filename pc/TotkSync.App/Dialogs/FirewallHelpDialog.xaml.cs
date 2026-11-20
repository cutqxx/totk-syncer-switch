using System.Windows;
using TotkSync.App.Services;
using Wpf.Ui.Controls;

namespace TotkSync.App.Dialogs;

public partial class FirewallHelpDialog : ContentDialog
{
    private readonly FirewallHelpModel _model;
    private readonly ClipboardService _clipboard;
    private readonly ShellOpener _shell;
    private readonly Toast _toast;

    public FirewallHelpDialog(
        ContentDialogHost? host,
        FirewallHelpModel model,
        ClipboardService clipboard,
        ShellOpener shell,
        Toast toast)
        : base(host)
    {
        _model = model;
        _clipboard = clipboard;
        _shell = shell;
        _toast = toast;
        DataContext = model;
        InitializeComponent();
    }

    private void OnCopyClick(object sender, RoutedEventArgs e)
    {
        if (_clipboard.TryCopy(_model.Command))
        {
            CopyButton.Content = "Copied";
            _toast.Show("Command copied");
            return;
        }
        ShowError("Couldn't copy to the clipboard. Select the command and press Ctrl+C.");
    }

    private void OnOpenNetworkSettingsClick(object sender, RoutedEventArgs e)
    {
        var error = _shell.OpenNetworkSettings();
        if (error is not null)
        {
            ShowError(error);
        }
    }

    private void ShowError(string message)
    {
        ErrorText.Text = message;
        ErrorText.Visibility = Visibility.Visible;
    }
}
