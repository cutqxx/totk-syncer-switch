using System.Windows.Controls;

namespace TotkSync.App.Pages.Home;

public partial class HomePage : Page
{
    public HomePage(HomeViewModel viewModel)
    {
        DataContext = viewModel;
        InitializeComponent();
        viewModel.GameFolderRejected += () => ChooseFolderButton.Focus();
        Loaded += (_, _) => PlayCardHost.FocusPlayWhenEnabled();
    }
}
