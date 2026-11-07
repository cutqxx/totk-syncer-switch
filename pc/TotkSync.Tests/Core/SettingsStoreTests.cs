using System.Text.Json;
using TotkSync.Core.Settings;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SettingsStoreTests
{
    [Fact]
    public void Missing_settings_file_loads_defaults()
    {
        using var temp = new TempDir();

        var settings = new SettingsStore(temp.Combine("settings.json")).Load();

        Assert.Equal(new LauncherSettings(null, null, 8765, null, 20), settings);
    }

    [Fact]
    public void Saved_settings_round_trip_including_token()
    {
        using var temp = new TempDir();
        var store = new SettingsStore(temp.Combine("settings.json"));
        var settings = new LauncherSettings(@"D:\Games\totk", @"D:\Saves\x", 9000, "secret-token", 5);

        store.Save(settings);

        Assert.Equal(settings, store.Load());
    }

    [Fact]
    public void Null_token_and_dirs_round_trip_as_null()
    {
        using var temp = new TempDir();
        var store = new SettingsStore(temp.Combine("settings.json"));

        store.Save(new LauncherSettings(GameDir: "C:/g"));

        var loaded = store.Load();
        Assert.Equal((null, null), (loaded.Token, loaded.SaveDir));
    }

    [Fact]
    public void Save_creates_missing_parent_directories()
    {
        using var temp = new TempDir();
        var path = temp.Combine("deep", "er", "settings.json");

        new SettingsStore(path).Save(new LauncherSettings());

        Assert.True(File.Exists(path));
    }

    [Fact]
    public void Save_leaves_no_temporary_file_behind()
    {
        using var temp = new TempDir();
        var store = new SettingsStore(temp.Combine("settings.json"));

        store.Save(new LauncherSettings(Port: 9001));
        store.Save(new LauncherSettings(Port: 9002));

        Assert.Equal(["settings.json"], Directory.GetFiles(temp.Path).Select(Path.GetFileName));
    }

    [Fact]
    public void Save_replaces_stale_temporary_file_from_an_interrupted_write()
    {
        using var temp = new TempDir();
        File.WriteAllText(temp.Combine("settings.json.tmp"), "{ half written");
        var store = new SettingsStore(temp.Combine("settings.json"));
        var settings = new LauncherSettings(Port: 9100);

        store.Save(settings);

        Assert.Equal(settings, store.Load());
    }

    [Fact]
    public void Stale_temporary_file_is_never_read_as_settings()
    {
        using var temp = new TempDir();
        File.WriteAllText(temp.Combine("settings.json.tmp"), "{ half written");

        Assert.Equal(new LauncherSettings(), new SettingsStore(temp.Combine("settings.json")).Load());
    }

    [Fact]
    public void Saved_file_uses_snake_case_property_names()
    {
        using var temp = new TempDir();
        var path = temp.Combine("settings.json");

        new SettingsStore(path).Save(new LauncherSettings(GameDir: "C:/g", KeepBackups: 7));

        var text = File.ReadAllText(path);
        Assert.Contains("\"game_dir\"", text);
        Assert.Contains("\"keep_backups\": 7", text);
    }

    [Fact]
    public void Corrupt_settings_file_throws_json_exception_and_is_left_untouched()
    {
        using var temp = new TempDir();
        var path = temp.Combine("settings.json");
        File.WriteAllText(path, "{ not json");

        Assert.ThrowsAny<JsonException>(() => new SettingsStore(path).Load());
        Assert.Equal("{ not json", File.ReadAllText(path));
    }

    [Fact]
    public void Settings_file_with_missing_fields_uses_defaults_for_them()
    {
        using var temp = new TempDir();
        var path = temp.Combine("settings.json");
        File.WriteAllText(path, "{\"game_dir\":\"C:/g\"}");

        Assert.Equal(new LauncherSettings(GameDir: "C:/g"), new SettingsStore(path).Load());
    }
}
