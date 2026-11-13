using System.Windows.Controls;

namespace TotkSync.App.Pages.Backups;

public partial class BackupsPage : Page
{
    public BackupsPage(BackupsViewModel viewModel)
    {
        DataContext = viewModel;
        InitializeComponent();
        Loaded += async (_, _) => await viewModel.RefreshAsync();
    }
}
