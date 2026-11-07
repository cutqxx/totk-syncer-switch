using System.Windows.Controls;
using Wpf.Ui.Controls;

namespace TotkSync.App.Pages.Settings;

public partial class SettingsPage : Page
{
    private readonly SettingsViewModel _viewModel;

    public SettingsPage(SettingsViewModel viewModel)
    {
        _viewModel = viewModel;
        DataContext = viewModel;
        InitializeComponent();
    }

    private async void OnPortValueChanged(object sender, NumberBoxValueChangedEventArgs args)
    {
        await _viewModel.CommitPortAsync(args.NewValue);
    }

    private async void OnKeepBackupsValueChanged(object sender, NumberBoxValueChangedEventArgs args)
    {
        await _viewModel.CommitKeepBackupsAsync(args.NewValue);
    }
}
