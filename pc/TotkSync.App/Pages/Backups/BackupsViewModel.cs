using System.Collections.ObjectModel;
using System.IO;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using TotkSync.App.Services;
using TotkSync.Core.Backups;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;

namespace TotkSync.App.Pages.Backups;

public sealed partial class BackupsViewModel : ObservableObject
{
    private readonly BackupStore _store;
    private readonly SettingsSession _settings;
    private readonly AppDataPaths _paths;
    private readonly ShellOpener _shell;
    private readonly UiThread _ui;

    public BackupsViewModel(
        BackupStore store,
        SettingsSession settings,
        AppDataPaths paths,
        ShellOpener shell,
        SaveStatusCache cache,
        UiThread ui)
    {
        _store = store;
        _settings = settings;
        _paths = paths;
        _shell = shell;
        _ui = ui;
        Rows = [];
        ListError = "";
        OpenFolderError = "";
        KeepingText = BackupPresenter.KeepingText(settings.Current.KeepBackups);
        cache.Changed += () => _ui.Post(() => _ = RefreshAsync());
        settings.Changed += () => _ui.Post(() => KeepingText = BackupPresenter.KeepingText(_settings.Current.KeepBackups));
    }

    public ObservableCollection<BackupRowModel> Rows { get; }

    public string FolderText => _paths.BackupsDir;

    public bool IsEmpty => !IsLoading && !HasListError && Rows.Count == 0;

    public bool HasRows => Rows.Count > 0;

    public bool HasOpenFolderError => OpenFolderError.Length > 0;

    public bool HasListError => ListError.Length > 0;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsEmpty))]
    public partial bool IsLoading { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsEmpty))]
    [NotifyPropertyChangedFor(nameof(HasListError))]
    public partial string ListError { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasOpenFolderError))]
    public partial string OpenFolderError { get; set; }

    [ObservableProperty]
    public partial string KeepingText { get; set; }

    public async Task RefreshAsync()
    {
        IsLoading = true;
        try
        {
            var entries = await Task.Run(_store.List);
            Rows.Clear();
            entries.Select(BackupPresenter.ToRow).ToList().ForEach(Rows.Add);
            ListError = "";
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            ListError = $"Couldn't read the backups folder: {error.Message}";
        }
        IsLoading = false;
        OnPropertyChanged(nameof(IsEmpty));
        OnPropertyChanged(nameof(HasRows));
    }

    [RelayCommand]
    private async Task OpenFolderAsync()
    {
        OpenFolderError = await _shell.OpenFolderAsync(_paths.BackupsDir) ?? "";
    }
}
