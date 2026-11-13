using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SaveStatusCacheGameGuardTests : IDisposable
{
    private readonly TempDir _temp = new();
    private readonly FakeGameProcess _game = new();
    private readonly CountingReader _reader;

    public SaveStatusCacheGameGuardTests()
    {
        _reader = new CountingReader(new SyncStateStore(_temp.Combine("state.json")));
    }

    public void Dispose() => _temp.Dispose();

    private SaveStatusCache NewCache(LauncherSettings settings)
    {
        var session = new SettingsSession(new SettingsStore(_temp.Combine("settings.json")), settings);
        return new SaveStatusCache(session, _reader, _game);
    }

    private static LauncherSettings Configured => new(GameDir: @"C:\any\game");

    [Fact]
    public async Task Running_game_with_no_status_yet_publishes_GameRunning_without_reading_the_save()
    {
        _game.Running = true;
        var cache = NewCache(Configured);

        await cache.RefreshAsync();

        Assert.IsType<SaveStatus.GameRunning>(cache.Current);
        Assert.Equal(0, _reader.Reads);
    }

    [Fact]
    public async Task Running_game_keeps_the_existing_status_and_raises_no_Changed()
    {
        var cache = NewCache(Configured);
        await cache.RefreshAsync();
        var known = cache.Current;
        var changes = 0;
        cache.Changed += () => changes++;
        _game.Running = true;

        await cache.RefreshAsync();

        Assert.Same(known, cache.Current);
        Assert.Equal((0, 1), (changes, _reader.Reads));
    }

    [Fact]
    public async Task Stopped_game_reads_the_save_as_before()
    {
        var cache = NewCache(Configured);

        await cache.RefreshAsync();

        Assert.Equal("read", Assert.IsType<SaveStatus.Unreadable>(cache.Current).Reason);
        Assert.Equal(1, _reader.Reads);
    }

    [Fact]
    public async Task Running_game_without_a_game_folder_is_NotConfigured()
    {
        _game.Running = true;
        var cache = NewCache(new LauncherSettings());

        await cache.RefreshAsync();

        Assert.IsType<SaveStatus.NotConfigured>(cache.Current);
        Assert.Equal(1, _reader.Reads);
    }

    private sealed class CountingReader(SyncStateStore syncState) : SaveStatusReader(syncState)
    {
        private int _reads;

        public int Reads => _reads;

        public override SaveStatus Read(LauncherSettings settings)
        {
            Interlocked.Increment(ref _reads);
            return string.IsNullOrWhiteSpace(settings.GameDir) ? new SaveStatus.NotConfigured() : new SaveStatus.Unreadable("read");
        }
    }
}
