using TotkSync.Core.Settings;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SettingsSessionTests
{
    private readonly TempDir _temp = new();

    private string SettingsPath => _temp.Combine("settings.json");

    private SettingsSession NewSession(LauncherSettings? initial = null)
    {
        return new SettingsSession(new SettingsStore(SettingsPath), initial ?? new LauncherSettings());
    }

    [Fact]
    public async Task Game_dir_with_totk_exe_is_saved_and_becomes_current()
    {
        using var _ = _temp;
        var gameDir = _temp.Combine("game");
        SaveTreeBuilder.WriteFile(gameDir, "totk.exe", "exe");
        var session = NewSession();

        var error = await session.SetGameDirAsync(gameDir);

        Assert.Null(error);
        Assert.Equal(gameDir, session.Current.GameDir);
        Assert.Equal(gameDir, new SettingsStore(SettingsPath).Load().GameDir);
    }

    [Fact]
    public async Task Game_dir_without_totk_exe_returns_error_and_is_not_saved()
    {
        using var _ = _temp;
        var gameDir = _temp.Combine("empty");
        Directory.CreateDirectory(gameDir);
        var session = NewSession();

        var error = await session.SetGameDirAsync(gameDir);

        Assert.Equal($"totk.exe not found in {gameDir}", error);
        Assert.Null(session.Current.GameDir);
        Assert.False(File.Exists(SettingsPath));
    }

    [Fact]
    public async Task Rejected_game_dir_keeps_the_previous_valid_one_on_disk()
    {
        using var _ = _temp;
        var good = _temp.Combine("good");
        SaveTreeBuilder.WriteFile(good, "totk.exe", "exe");
        var session = NewSession();
        await session.SetGameDirAsync(good);

        await session.SetGameDirAsync(_temp.Combine("bad"));

        Assert.Equal(good, new SettingsStore(SettingsPath).Load().GameDir);
        Assert.Equal(good, session.Current.GameDir);
    }

    [Theory]
    [InlineData(1023)]
    [InlineData(65536)]
    public async Task Invalid_port_returns_error_and_is_not_saved(int port)
    {
        using var _ = _temp;
        var session = NewSession();

        var error = await session.SetPortAsync(port);

        Assert.NotNull(error);
        Assert.Equal(8765, session.Current.Port);
        Assert.False(File.Exists(SettingsPath));
    }

    [Theory]
    [InlineData(1024)]
    [InlineData(65535)]
    public async Task Valid_port_is_saved(int port)
    {
        using var _ = _temp;
        var session = NewSession();

        var error = await session.SetPortAsync(port);

        Assert.Null(error);
        Assert.Equal(port, new SettingsStore(SettingsPath).Load().Port);
    }

    [Fact]
    public async Task Zero_keep_backups_returns_error_and_is_not_saved()
    {
        using var _ = _temp;
        var session = NewSession();

        var error = await session.SetKeepBackupsAsync(0);

        Assert.Equal("Keep at least 1 backup.", error);
        Assert.Equal(20, session.Current.KeepBackups);
        Assert.False(File.Exists(SettingsPath));
    }

    [Fact]
    public async Task One_keep_backup_is_saved()
    {
        using var _ = _temp;
        var session = NewSession();

        var error = await session.SetKeepBackupsAsync(1);

        Assert.Null(error);
        Assert.Equal(1, new SettingsStore(SettingsPath).Load().KeepBackups);
    }

    [Fact]
    public async Task Save_dir_is_saved_and_blank_clears_it()
    {
        using var _ = _temp;
        var session = NewSession();

        await session.SetSaveDirAsync(@"D:\custom");
        var afterSet = new SettingsStore(SettingsPath).Load().SaveDir;
        await session.SetSaveDirAsync("  ");

        Assert.Equal(@"D:\custom", afterSet);
        Assert.Null(new SettingsStore(SettingsPath).Load().SaveDir);
    }

    [Fact]
    public async Task Changing_one_setting_preserves_the_others_including_token()
    {
        using var _ = _temp;
        var session = NewSession(new LauncherSettings(Token: "tok", Port: 9000));

        await session.SetKeepBackupsAsync(3);

        Assert.Equal(new LauncherSettings(Token: "tok", Port: 9000, KeepBackups: 3), new SettingsStore(SettingsPath).Load());
    }

    [Fact]
    public async Task Changed_event_fires_on_success_and_not_on_rejection()
    {
        using var _ = _temp;
        var session = NewSession();
        var fired = 0;
        session.Changed += () => fired++;

        await session.SetPortAsync(80);
        await session.SetPortAsync(9000);

        Assert.Equal(1, fired);
    }
}
