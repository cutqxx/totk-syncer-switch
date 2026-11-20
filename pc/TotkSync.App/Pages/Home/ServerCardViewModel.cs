using System.Net;
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
    private readonly ClipboardService _clipboard;
    private readonly Toast _toast;
    private IPAddress? _preferredIp;

    public ServerCardViewModel(
        ServerHost host,
        ActivityLog activity,
        UiThread ui,
        Navigator navigator,
        PairingViewModel pairing,
        ClipboardService clipboard,
        Toast toast)
    {
        _host = host;
        _navigator = navigator;
        _ui = ui;
        _clipboard = clipboard;
        _toast = toast;
        Pairing = pairing;
        Pairing.IsServerRunning = host.State.Phase == ServerPhase.Running;
        Card = ServerCardPresenter.From(host.State);
        LastRequestText = NoRequestsText;
        host.Changed += ApplyState;
        activity.EntryAdded += entry => _ui.Post(() => ShowLastRequest(entry));
    }

    public PairingViewModel Pairing { get; }

    [ObservableProperty]
    public partial ServerCardModel Card { get; set; }

    [ObservableProperty]
    public partial string LastRequestText { get; set; }

    [ObservableProperty]
    public partial ServerAddress? SelectedAddress { get; set; }

    [RelayCommand]
    private Task RetryAsync() => _host.StartAsync();

    [RelayCommand]
    private void CopyAddress()
    {
        if (SelectedAddress is { } address && _clipboard.TryCopy(address.Url))
        {
            _toast.Show("Address copied");
        }
    }

    [RelayCommand]
    private void ChangePort() => _navigator.ShowSettings();

    private void ApplyState()
    {
        Card = ServerCardPresenter.From(_host.State);
        Pairing.IsServerRunning = _host.State.Phase == ServerPhase.Running;
        SelectedAddress = PreferredAddress() ?? Card.FirstAddress;
    }

    private ServerAddress? PreferredAddress()
    {
        return _preferredIp is { } preferred
            ? Card.Addresses.FirstOrDefault(address => address.Ip.Equals(preferred))
            : null;
    }

    partial void OnSelectedAddressChanged(ServerAddress? value)
    {
        if (value is not null)
        {
            _preferredIp = value.Ip;
        }
        Pairing.SelectedAddress = value;
    }

    private void ShowLastRequest(ActivityEntry entry)
    {
        LastRequestText = $"Last request: {DisplayFormat.TimeWithSeconds(entry.Time)} from {entry.Client}";
    }
}
