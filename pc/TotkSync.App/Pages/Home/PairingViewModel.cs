using System.IO;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TotkSync.App.Services;
using TotkSync.Core.Pairing;
using TotkSync.Core.Settings;

namespace TotkSync.App.Pages.Home;

public sealed partial class PairingViewModel : ObservableObject
{
    public const string HiddenTokenText = "••••••••••••••••";

    private readonly SettingsSession _settings;
    private readonly ClipboardService _clipboard;
    private readonly SaveFilePicker _picker;
    private readonly ShellOpener _shell;
    private readonly Toast _toast;
    private readonly FirewallHelpPresenter _firewall;

    public PairingViewModel(
        SettingsSession settings,
        ClipboardService clipboard,
        SaveFilePicker picker,
        ShellOpener shell,
        Toast toast,
        FirewallHelpPresenter firewall)
    {
        _settings = settings;
        _clipboard = clipboard;
        _picker = picker;
        _shell = shell;
        _toast = toast;
        _firewall = firewall;
        PairingError = "";
        SavedFolder = "";
    }

    public ServerAddress? SelectedAddress { get; set; }

    public bool IsServerRunning { get; set; }

    public string TokenText => IsTokenVisible ? _settings.Current.Token ?? "" : HiddenTokenText;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(TokenText))]
    public partial bool IsTokenVisible { get; set; }

    [ObservableProperty]
    public partial string PairingError { get; set; }

    [ObservableProperty]
    public partial bool IsSavedVisible { get; set; }

    [ObservableProperty]
    public partial string SavedFolder { get; set; }

    [RelayCommand]
    private void ToggleToken() => IsTokenVisible = !IsTokenVisible;

    [RelayCommand]
    private void CopyToken()
    {
        var copied = _clipboard.TryCopySecret(_settings.Current.Token ?? "");
        ReportCopy(copied, "Token copied");
    }

    [RelayCommand]
    private async Task SaveSwitchConfigAsync()
    {
        if (!IsServerRunning)
        {
            ShowPairingError("Server isn't running — fix the server error first.");
            return;
        }
        if (SelectedAddress is not { } address)
        {
            ShowPairingError("No home network address found. Connect this PC to the same network as the Switch.");
            return;
        }
        var path = _picker.Pick("Save switch-config.json", SwitchConfigFile.FileName);
        if (path is null)
        {
            return;
        }
        var port = _settings.Current.Port;
        var token = _settings.Current.Token ?? "";
        var error = await Task.Run(() => SwitchConfigFile.Save(path, address.Ip, port, token));
        ShowSaveOutcome(error, path);
    }

    [RelayCommand]
    private async Task OpenSavedFolderAsync()
    {
        PairingError = await _shell.OpenFolderAsync(SavedFolder) ?? "";
    }

    [RelayCommand]
    public Task ShowFirewallHelpAsync() => _firewall.ShowAsync(SelectedAddress);

    private void ShowSaveOutcome(string? error, string path)
    {
        if (error is not null)
        {
            ShowPairingError(error);
            return;
        }
        PairingError = "";
        SavedFolder = Path.GetDirectoryName(path) ?? "";
        IsSavedVisible = true;
    }

    private void ReportCopy(bool copied, string confirmation)
    {
        if (copied)
        {
            PairingError = "";
            _toast.Show(confirmation);
            return;
        }
        ShowPairingError("Couldn't copy to the clipboard. Try again.");
    }

    private void ShowPairingError(string message)
    {
        IsSavedVisible = false;
        PairingError = message;
    }
}
