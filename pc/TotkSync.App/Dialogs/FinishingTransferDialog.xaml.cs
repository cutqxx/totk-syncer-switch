using Wpf.Ui.Controls;

namespace TotkSync.App.Dialogs;

public partial class FinishingTransferDialog : ContentDialog
{
    public FinishingTransferDialog(ContentDialogHost? host)
        : base(host)
    {
        InitializeComponent();
    }
}
