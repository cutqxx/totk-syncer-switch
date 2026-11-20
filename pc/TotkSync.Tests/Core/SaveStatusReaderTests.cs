using TotkSync.Core.Saves;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SaveStatusReaderTests : IDisposable
{
    private readonly TempDir _temp = new();

    public void Dispose() => _temp.Dispose();

    private string StatePath => _temp.Combine("state", "state.json");

    private SaveStatusReader Reader() => new(new SyncStateStore(StatePath));

    private string GameDirWithSave(string profile = "A")
    {
        var gameDir = _temp.Combine("game");
        SaveTreeBuilder.BuildGameDirWithSave(gameDir, profile);
        return gameDir;
    }

    [Fact]
    public void No_game_dir_is_not_configured()
    {
        Assert.IsType<SaveStatus.NotConfigured>(Reader().Read(new LauncherSettings()));
    }

    [Fact]
    public void Game_dir_without_totk_exe_is_not_configured()
    {
        Directory.CreateDirectory(_temp.Combine("game"));

        Assert.IsType<SaveStatus.NotConfigured>(Reader().Read(new LauncherSettings(GameDir: _temp.Combine("game"))));
    }

    [Fact]
    public void Configured_game_without_save_is_not_found()
    {
        var gameDir = _temp.Combine("game");
        SaveTreeBuilder.WriteFile(gameDir, "totk.exe", "exe");

        Assert.IsType<SaveStatus.NotFound>(Reader().Read(new LauncherSettings(GameDir: gameDir)));
    }

    [Fact]
    public void Several_candidates_are_listed_and_none_selected()
    {
        var gameDir = GameDirWithSave("A");
        SaveTreeBuilder.BuildMinimalSave(Path.Combine(gameDir, "profile", "B"));

        var status = Assert.IsType<SaveStatus.Several>(Reader().Read(new LauncherSettings(GameDir: gameDir)));

        Assert.Equal(2, status.Items.Count);
    }

    [Fact]
    public void Save_dir_override_resolves_ambiguity()
    {
        var gameDir = GameDirWithSave("A");
        var second = SaveTreeBuilder.BuildMinimalSave(Path.Combine(gameDir, "profile", "B"));

        var status = Assert.IsType<SaveStatus.Found>(
            Reader().Read(new LauncherSettings(GameDir: gameDir, SaveDir: second)));

        Assert.Equal(second, status.Snapshot.Dir);
    }

    [Fact]
    public void Non_existent_save_dir_override_is_not_found()
    {
        var gameDir = GameDirWithSave();

        var status = Reader().Read(new LauncherSettings(GameDir: gameDir, SaveDir: _temp.Combine("nope")));

        Assert.IsType<SaveStatus.NotFound>(status);
    }

    [Fact]
    public void Found_save_without_state_file_is_never_synced()
    {
        var status = Assert.IsType<SaveStatus.Found>(Reader().Read(new LauncherSettings(GameDir: GameDirWithSave())));

        Assert.Equal((null, SyncStatus.NeverSynced), (status.LastSync, status.Sync));
    }

    [Fact]
    public void Found_save_matching_last_synced_fingerprint_is_in_sync()
    {
        var gameDir = GameDirWithSave();
        var fingerprint = SaveSnapshot.Capture(Path.Combine(gameDir, "profile", "A")).Fingerprint;
        WriteState(fingerprint);

        var status = Assert.IsType<SaveStatus.Found>(Reader().Read(new LauncherSettings(GameDir: gameDir)));

        Assert.Equal(SyncStatus.InSync, status.Sync);
    }

    [Fact]
    public void Found_save_changed_after_last_sync_has_new_progress()
    {
        var gameDir = GameDirWithSave();
        WriteState(SaveSnapshot.Capture(Path.Combine(gameDir, "profile", "A")).Fingerprint);
        SaveTreeBuilder.WriteFile(gameDir, "profile/A/option.sav", "played more");

        var status = Assert.IsType<SaveStatus.Found>(Reader().Read(new LauncherSettings(GameDir: gameDir)));

        Assert.Equal(SyncStatus.PcHasNewProgress, status.Sync);
    }

    [Fact]
    public void Corrupt_state_file_gives_unreadable_status_instead_of_throwing()
    {
        var gameDir = GameDirWithSave();
        Directory.CreateDirectory(Path.GetDirectoryName(StatePath)!);
        File.WriteAllText(StatePath, "{ broken");

        Assert.IsType<SaveStatus.Unreadable>(Reader().Read(new LauncherSettings(GameDir: gameDir)));
    }

    [Fact]
    public void Found_snapshot_excludes_junk_from_counts()
    {
        var gameDir = GameDirWithSave();
        SaveTreeBuilder.WriteFile(gameDir, "profile/A/Thumbs.db", "junk");
        SaveTreeBuilder.WriteFile(gameDir, "profile/A/old.7z", "junk");

        var status = Assert.IsType<SaveStatus.Found>(Reader().Read(new LauncherSettings(GameDir: gameDir)));

        Assert.Equal(2, status.Snapshot.Files);
    }

    private void WriteState(string fingerprint)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(StatePath)!);
        File.WriteAllText(
            StatePath,
            "{\"last_sync\":{\"fingerprint\":\"" + fingerprint
            + "\",\"at\":\"2026-11-03T14:25:00+00:00\",\"direction\":\"switch->pc\"}}");
    }
}
