using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using TotkSync.App.Services;
using TotkSync.Core.Activity;

namespace TotkSync.App.Pages.Activity;

public sealed partial class ActivityViewModel : ObservableObject
{
    private static readonly TimeSpan LoadingIndicatorDelay = TimeSpan.FromMilliseconds(300);

    private readonly ActivityLog _log;
    private readonly UiThread _ui;
    private bool _isLoaded;

    public ActivityViewModel(ActivityLog log, UiThread ui)
    {
        _log = log;
        _ui = ui;
        Rows = [];
        _log.EntryAdded += entry => _ui.Post(() => InsertNewest(entry));
    }

    public ObservableCollection<ActivityRowModel> Rows { get; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsEmpty))]
    public partial bool IsLoading { get; set; }

    [ObservableProperty]
    public partial bool IsLoadingIndicatorVisible { get; set; }

    [ObservableProperty]
    public partial bool IsUnreadable { get; set; }

    public bool IsEmpty => !IsLoading && Rows.Count == 0;

    public async Task InitializeAsync()
    {
        IsLoading = true;
        var loading = _log.LoadAsync();
        _ = ShowLoadingIndicatorIfSlowAsync();
        var result = await loading;
        IsUnreadable = result is LoadResult.Unreadable;
        ShowAll(_log.Snapshot());
        _isLoaded = true;
        IsLoading = false;
        IsLoadingIndicatorVisible = false;
    }

    private async Task ShowLoadingIndicatorIfSlowAsync()
    {
        await Task.Delay(LoadingIndicatorDelay);
        IsLoadingIndicatorVisible = IsLoading;
    }

    private void ShowAll(IReadOnlyList<ActivityEntry> entries)
    {
        Rows.Clear();
        foreach (var entry in entries.Reverse())
        {
            Rows.Add(ActivityPresenter.ToRow(entry));
        }
        OnPropertyChanged(nameof(IsEmpty));
    }

    private void InsertNewest(ActivityEntry entry)
    {
        if (!_isLoaded || Rows.Any(row => ReferenceEquals(row.Entry, entry)))
        {
            return;
        }
        Rows.Insert(0, ActivityPresenter.ToRow(entry));
        if (Rows.Count > ActivityLog.DefaultCapacity)
        {
            Rows.RemoveAt(Rows.Count - 1);
        }
        OnPropertyChanged(nameof(IsEmpty));
    }
}
