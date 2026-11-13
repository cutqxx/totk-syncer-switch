using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TotkSync.App.Services;
using TotkSync.Core.Game;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;

namespace TotkSync.App.Pages.Home;

public sealed partial class PlayCardViewModel : ObservableObject
{
    private readonly GameLauncher _launcher;
    private readonly GameWatcher _watcher;
    private readonly SettingsSession _settings;
    private readonly SaveStatusCache _cache;
    private readonly UiThread _ui;

    public PlayCardViewModel(
        GameLauncher launcher,
        GameWatcher watcher,
        SettingsSession settings,
        SaveStatusCache cache,
        UiThread ui)
    {
        _launcher = launcher;
        _watcher = watcher;
        _settings = settings;
        _cache = cache;
        _ui = ui;
        Card = Present();
        LaunchError = "";
        IsGameRunning = watcher.IsRunning;
        _cache.Changed += () => _ui.Post(Refresh);
        _settings.Changed += () => _ui.Post(() => OnPropertyChanged(nameof(GameDir)));
        _launcher.PhaseChanged += _ => _ui.Post(Refresh);
        _watcher.RunningChanged += OnRunningChanged;
        _watcher.PcSaveHasNewProgress += () => _ui.Post(() => HasNewProgress = true);
    }

    public string NewProgressMessage => GameMessages.PcSaveHasNewProgress;

    public string GameDir => _settings.Current.GameDir ?? "";

    [ObservableProperty]
    public partial PlayCardModel Card { get; set; }

    [ObservableProperty]
    public partial bool IsGameRunning { get; set; }

    [ObservableProperty]
    public partial bool HasNewProgress { get; set; }

    [ObservableProperty]
    public partial string LaunchError { get; set; }

    public bool HasLaunchError
    {
        get => LaunchError.Length > 0;
        set
        {
            if (!value)
            {
                LaunchError = "";
            }
        }
    }

    [RelayCommand]
    private async Task PlayAsync()
    {
        LaunchError = "";
        var result = await _launcher.PlayAsync(GameDir, CancellationToken.None);
        if (result is LaunchResult.Failed failed)
        {
            LaunchError = failed.Reason;
        }
        await _watcher.TickAsync(CancellationToken.None);
    }

    partial void OnLaunchErrorChanged(string value) => OnPropertyChanged(nameof(HasLaunchError));

    private void OnRunningChanged(bool isRunning)
    {
        _ui.Post(() =>
        {
            IsGameRunning = isRunning;
            HasNewProgress &= !isRunning;
            Refresh();
        });
    }

    private void Refresh() => Card = Present();

    private PlayCardModel Present()
    {
        var status = _cache.Current;
        var isConfigured = status is not null and not SaveStatus.NotConfigured;
        var inputs = new PlayInputs(isConfigured, status is SaveStatus.NotFound, _watcher.IsRunning, _launcher.Phase);
        return PlayCardPresenter.From(inputs);
    }
}
