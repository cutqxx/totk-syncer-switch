using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TotkSync.App.Services;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;

namespace TotkSync.App.Pages.Home;

public sealed partial class HomeViewModel : ObservableObject
{
    private readonly SettingsSession _settings;
    private readonly SaveStatusCache _cache;
    private readonly FolderPicker _picker;
    private readonly ShellOpener _shell;
    private readonly Navigator _navigator;
    private bool _setupInProgress;

    public HomeViewModel(
        SettingsSession settings,
        SaveStatusCache cache,
        FolderPicker picker,
        ShellOpener shell,
        Navigator navigator,
        ServerCardViewModel serverCard)
    {
        _settings = settings;
        _cache = cache;
        _picker = picker;
        _shell = shell;
        _navigator = navigator;
        ServerCard = serverCard;
        Card = SaveCardModel.Empty;
        GameFolderError = "";
        SaveFolderError = "";
        OpenFolderError = "";
        IsLoading = true;
        _cache.Changed += ApplyStatus;
        ApplyStatus();
    }

    public event Action? GameFolderRejected;

    public ServerCardViewModel ServerCard { get; }

    [ObservableProperty]
    public partial SaveCardModel Card { get; set; }

    [ObservableProperty]
    public partial string GameFolderError { get; set; }

    [ObservableProperty]
    public partial string SaveFolderError { get; set; }

    [ObservableProperty]
    public partial string OpenFolderError { get; set; }

    [ObservableProperty]
    public partial bool IsLoading { get; set; }

    [ObservableProperty]
    public partial bool IsConfigured { get; set; }

    [ObservableProperty]
    public partial bool IsSetupVisible { get; set; }

    [ObservableProperty]
    public partial bool IsSaveCardVisible { get; set; }

    [RelayCommand]
    private async Task ChooseFolderAsync()
    {
        var path = _picker.Pick("Choose your TotK PC folder");
        if (path is null)
        {
            return;
        }
        var error = await _settings.SetGameDirAsync(path);
        GameFolderError = error ?? "";
        if (error is not null)
        {
            GameFolderRejected?.Invoke();
            return;
        }
        _setupInProgress = true;
        UpdateVisibility();
    }

    [RelayCommand]
    private void FinishSetup()
    {
        _setupInProgress = false;
        UpdateVisibility();
    }

    [RelayCommand]
    private async Task UseCandidateAsync(string dir) => SaveFolderError = await _settings.SetSaveDirAsync(dir) ?? "";

    [RelayCommand]
    private async Task OpenSaveFolderAsync() => OpenFolderError = await _shell.OpenFolderAsync(Card.Folder) ?? "";

    [RelayCommand]
    private void OpenSettings() => _navigator.ShowSettings();

    private void ApplyStatus()
    {
        var status = _cache.Current;
        IsLoading = status is null;
        IsConfigured = status is not null and not SaveStatus.NotConfigured;
        Card = status is null ? SaveCardModel.Empty : SaveCardPresenter.From(status);
        UpdateVisibility();
    }

    private void UpdateVisibility()
    {
        IsSetupVisible = !IsLoading && (!IsConfigured || _setupInProgress);
        IsSaveCardVisible = !IsLoading && !IsSetupVisible;
    }
}
