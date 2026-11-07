using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TotkSync.App.Services;
using TotkSync.Core.Activity;
using TotkSync.Core.Display;
using TotkSync.Server;

namespace TotkSync.App.Pages.Home;

public sealed partial class ServerCardViewModel : ObservableObject
{
    private const string NoRequestsText = "No requests from the Switch yet";

    private readonly ServerHost _host;
    private readonly Navigator _navigator;
    private readonly UiThread _ui;

    public ServerCardViewModel(ServerHost host, ActivityLog activity, UiThread ui, Navigator navigator)
    {
        _host = host;
        _navigator = navigator;
        _ui = ui;
        Card = ServerCardPresenter.From(host.State);
        LastRequestText = NoRequestsText;
        host.Changed += ApplyState;
        activity.EntryAdded += entry => _ui.Post(() => ShowLastRequest(entry));
    }

    [ObservableProperty]
    public partial ServerCardModel Card { get; set; }

    [ObservableProperty]
    public partial string LastRequestText { get; set; }

    [ObservableProperty]
    public partial string? SelectedAddress { get; set; }

    [RelayCommand]
    private Task RetryAsync() => _host.StartAsync();

    [RelayCommand]
    private void ChangePort() => _navigator.ShowSettings();

    private void ApplyState()
    {
        Card = ServerCardPresenter.From(_host.State);
        SelectedAddress = Card.Addresses.Contains(SelectedAddress ?? "") ? SelectedAddress : Card.FirstAddress;
    }

    private void ShowLastRequest(ActivityEntry entry)
    {
        LastRequestText = $"Last request: {DisplayFormat.TimeWithSeconds(entry.Time)} from {entry.Client}";
    }
}
