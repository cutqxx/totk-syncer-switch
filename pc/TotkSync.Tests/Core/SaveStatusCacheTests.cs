using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SaveStatusCacheTests : IDisposable
{
    private readonly TempDir _temp = new();

    public void Dispose() => _temp.Dispose();

    private SaveStatusCache NewCache(SettingsSession session)
    {
        return new SaveStatusCache(session, new SaveStatusReader(new SyncStateStore(_temp.Combine("state.json"))));
    }

    private SettingsSession NewSession(LauncherSettings settings)
    {
        return new SettingsSession(new SettingsStore(_temp.Combine("settings.json")), settings);
    }

    [Fact]
    public void Current_is_null_until_the_first_refresh()
    {
        var cache = NewCache(NewSession(new LauncherSettings()));

        Assert.Null(cache.Current);
    }

    [Fact]
    public async Task Refresh_publishes_the_status_read_from_current_settings()
    {
        var cache = NewCache(NewSession(new LauncherSettings()));

        await cache.RefreshAsync();

        Assert.IsType<SaveStatus.NotConfigured>(cache.Current);
    }

    [Fact]
    public async Task Refresh_raises_changed_once_after_the_status_is_published()
    {
        var cache = NewCache(NewSession(new LauncherSettings()));
        var seenDuringEvent = new List<SaveStatus?>();
        cache.Changed += () => seenDuringEvent.Add(cache.Current);

        await cache.RefreshAsync();

        Assert.IsType<SaveStatus.NotConfigured>(Assert.Single(seenDuringEvent));
    }

    [Fact]
    public async Task Changing_the_game_dir_in_settings_refreshes_the_cache_to_the_new_save()
    {
        var gameDir = _temp.Combine("game");
        SaveTreeBuilder.BuildGameDirWithSave(gameDir, "A");
        var session = NewSession(new LauncherSettings());
        var cache = NewCache(session);
        var refreshed = new TaskCompletionSource();
        cache.Changed += () => refreshed.TrySetResult();

        await session.SetGameDirAsync(gameDir);
        await refreshed.Task.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.IsType<SaveStatus.Found>(cache.Current);
    }

    [Fact]
    public async Task Overlapping_refreshes_end_with_the_state_of_the_latest_settings()
    {
        var gameDir = _temp.Combine("game");
        SaveTreeBuilder.BuildGameDirWithSave(gameDir, "A");
        var session = NewSession(new LauncherSettings(GameDir: gameDir));
        var cache = NewCache(session);

        await Task.WhenAll(Enumerable.Range(0, 10).Select(_ => cache.RefreshAsync()));

        Assert.IsType<SaveStatus.Found>(cache.Current);
    }

    [Fact]
    public async Task Stale_result_computed_for_older_settings_is_discarded()
    {
        var gameDir = _temp.Combine("game");
        SaveTreeBuilder.BuildGameDirWithSave(gameDir, "A");
        var session = NewSession(new LauncherSettings(GameDir: gameDir, SaveDir: GatedReader.SlowMarker));
        var reader = new GatedReader(new SyncStateStore(_temp.Combine("state.json")));
        var cache = new SaveStatusCache(session, reader);
        var changes = 0;
        var latestPublished = new TaskCompletionSource();
        cache.Changed += () =>
        {
            changes++;
            latestPublished.TrySetResult();
        };

        var stale = cache.RefreshAsync();
        await reader.SlowReadStarted.Task.WaitAsync(TimeSpan.FromSeconds(10));
        await session.SetSaveDirAsync(null);
        await latestPublished.Task.WaitAsync(TimeSpan.FromSeconds(10));
        reader.ReleaseSlowRead();
        await stale;

        Assert.IsType<SaveStatus.Found>(cache.Current);
        Assert.Equal(1, changes);
    }

    private sealed class GatedReader(SyncStateStore syncState) : SaveStatusReader(syncState)
    {
        public const string SlowMarker = "slow-marker";

        private readonly ManualResetEventSlim _release = new();

        public TaskCompletionSource SlowReadStarted { get; } = new();

        public void ReleaseSlowRead() => _release.Set();

        public override SaveStatus Read(LauncherSettings settings)
        {
            if (settings.SaveDir != SlowMarker)
            {
                return base.Read(settings);
            }
            SlowReadStarted.TrySetResult();
            _release.Wait(TimeSpan.FromSeconds(10));
            return new SaveStatus.Unreadable("stale");
        }
    }
}
