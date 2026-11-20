using Wpf.Ui;
using Wpf.Ui.Controls;

namespace TotkSync.App.Services;

public sealed class Toast(ISnackbarService snackbar)
{
    private static readonly TimeSpan Duration = TimeSpan.FromSeconds(3);

    public void Show(string message)
    {
        snackbar.Show(message, "", ControlAppearance.Success, new SymbolIcon(SymbolRegular.CheckmarkCircle24), Duration);
    }
}
