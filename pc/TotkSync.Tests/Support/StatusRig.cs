using TotkSync.Core.Game;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;

namespace TotkSync.Tests.Support;

public sealed class FakeGameProcess : IGameProcess
{
    public bool Running { get; set; }

    public bool IsRunning() => Running;
}

public sealed class StatusRig
{
    private StatusRig(SaveStatusCache cache, FakeGameProcess game, string saveDir)
    {
        Cache = cache;
        Game = game;
        SaveDir = saveDir;
        Query = new StatusQuery(cache, game);
    }

    public SaveStatusCache Cache { get; }

    public FakeGameProcess Game { get; }

    public StatusQuery Query { get; }

    public string SaveDir { get; }

    public static async Task<StatusRig> CreateAsync(string root, bool withSave, string? lastSyncJson = null)
    {
        var gameDir = Path.Combine(root, "game");
        var saveDir = withSave
            ? SaveTreeBuilder.BuildGameDirWithSave(gameDir, "A")
            : WriteGameExeOnly(gameDir);
        var statePath = Path.Combine(root, "state", "state.json");
        if (lastSyncJson is not null)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(statePath)!);
            File.WriteAllText(statePath, lastSyncJson);
        }
        var session = new SettingsSession(new SettingsStore(Path.Combine(root, "settings.json")), new LauncherSettings(GameDir: gameDir));
        var cache = new SaveStatusCache(session, new SaveStatusReader(new SyncStateStore(statePath)));
        await cache.RefreshAsync();
        return new StatusRig(cache, new FakeGameProcess(), saveDir);
    }

    public static string LastSyncJson(string direction)
    {
        return $$$"""{"last_sync":{"fingerprint":"sha256:abc","at":"2026-11-03T12:00:00+03:00","direction":"{{{direction}}}"}}""";
    }

    private static string WriteGameExeOnly(string gameDir)
    {
        SaveTreeBuilder.WriteFile(gameDir, "totk.exe", "exe");
        return Path.Combine(gameDir, "profile");
    }
}
