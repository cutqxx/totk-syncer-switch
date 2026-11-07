using System.Reflection;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TotkSync.App.Services;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;

namespace TotkSync.App.Pages.Settings;

public sealed partial class SettingsViewModel : ObservableObject
{
    private const int InvalidNumber = 0;

    private readonly SettingsSession _settings;
    private readonly SaveStatusCache _cache;
    private readonly FolderPicker _picker;
    private readonly ShellOpener _shell;
    private readonly AppDataPaths _paths;

    public SettingsViewModel(
        SettingsSession settings,
        SaveStatusCache cache,
        FolderPicker picker,
        ShellOpener shell,
        AppDataPaths paths)
    {
        _settings = settings;
        _cache = cache;
        _picker = picker;
        _shell = shell;
        _paths = paths;
        GameFolderError = "";
        SaveFolderError = "";
        DataFolderError = "";
        PortError = "";
        KeepBackupsError = "";
        Refresh();
        _settings.Changed += Refresh;
        _cache.Changed += Refresh;
    }

    public string AboutTitle { get; } = $"TotK Save Sync {Version()}";

    public string DataFolderText => $"Data folder: {_paths.Root} (settings, activity log, backups)";

    public string GameFolderText => _settings.Current.GameDir ?? "Not set";

    public string SaveFolderText => DescribeSaveFolder();

    public bool IsSaveFolderCustom => _settings.Current.SaveDir is not null;

    public double PortValue => _settings.Current.Port;

    public double KeepBackupsValue => _settings.Current.KeepBackups;

    [ObservableProperty]
    public partial string GameFolderError { get; set; }

    [ObservableProperty]
    public partial string SaveFolderError { get; set; }

    [ObservableProperty]
    public partial string DataFolderError { get; set; }

    [ObservableProperty]
    public partial string PortError { get; set; }

    [ObservableProperty]
    public partial string KeepBackupsError { get; set; }

    public async Task CommitPortAsync(double? value)
    {
        var port = AsInteger(value);
        PortError = port == _settings.Current.Port ? "" : await _settings.SetPortAsync(port) ?? "";
    }

    public async Task CommitKeepBackupsAsync(double? value)
    {
        var keepBackups = AsInteger(value);
        KeepBackupsError = keepBackups == _settings.Current.KeepBackups
            ? ""
            : await _settings.SetKeepBackupsAsync(keepBackups) ?? "";
    }

    [RelayCommand]
    private async Task BrowseGameFolderAsync()
    {
        var path = _picker.Pick("Choose your TotK PC folder");
        if (path is not null)
        {
            GameFolderError = await _settings.SetGameDirAsync(path) ?? "";
        }
    }

    [RelayCommand]
    private async Task BrowseSaveFolderAsync()
    {
        var path = _picker.Pick("Choose the PC save folder");
        if (path is not null)
        {
            SaveFolderError = await _settings.SetSaveDirAsync(path) ?? "";
        }
    }

    [RelayCommand]
    private async Task UseAutomaticSaveFolderAsync() => SaveFolderError = await _settings.SetSaveDirAsync(null) ?? "";

    [RelayCommand]
    private async Task OpenDataFolderAsync() => DataFolderError = await _shell.OpenAppDataFolderAsync(_paths) ?? "";

    private void Refresh()
    {
        OnPropertyChanged(nameof(GameFolderText));
        OnPropertyChanged(nameof(SaveFolderText));
        OnPropertyChanged(nameof(IsSaveFolderCustom));
        OnPropertyChanged(nameof(PortValue));
        OnPropertyChanged(nameof(KeepBackupsValue));
    }

    private string DescribeSaveFolder()
    {
        if (_settings.Current.SaveDir is { } custom)
        {
            return $"Custom: {custom}";
        }
        return _cache.Current is SaveStatus.Found found
            ? $"Automatic: {found.Snapshot.Dir}"
            : "Automatic: not found";
    }

    private static int AsInteger(double? value)
    {
        return value is { } number && number == Math.Floor(number) && Math.Abs(number) < int.MaxValue
            ? (int)number
            : InvalidNumber;
    }

    private static string Version()
    {
        var version = Assembly.GetExecutingAssembly().GetName().Version;
        return version is null ? "" : $"{version.Major}.{version.Minor}.{version.Build}";
    }
}
