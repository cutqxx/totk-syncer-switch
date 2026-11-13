using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Backups;
using TotkSync.Core.Replace;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;

namespace TotkSync.Tests.Support;

/// <summary>A game folder with a realistic PC save and the whole receiving stack wired to one FaultyFileOps.</summary>
public sealed class ReceiverRig : IDisposable
{
    public static readonly DateTimeOffset FakeNow = new(2026, 11, 7, 10, 30, 15, TimeSpan.Zero);
    public const string ExpectedBackupName = "pc-20261107-103015-before-switch.zip";

    private const string SaveKey = "profile/A";

    private ReceiverRig(TempDir temp, LauncherSettings settings)
    {
        Temp = temp;
        GameDir = settings.GameDir!;
        Settings = new SettingsSession(new SettingsStore(temp.Combine("settings.json")), settings);
        SyncState = new SyncStateStore(temp.Combine("state", "state.json"));
        Cache = new SaveStatusCache(Settings, new SaveStatusReader(SyncState));
        Query = new StatusQuery(Cache, Game);
        Time = new FakeTimeProvider(FakeNow);
        Ops = new FaultyFileOps(new FileOps());
        BackupsDir = temp.Combine("backups");
        Backups = new BackupStore(BackupsDir, Ops, Time);
        Replacer = new MirrorReplacer(Ops, Game, SwapRetry.None);
        Receiver = new SaveReceiver(Settings, Game, Gate, Cache, Backups, Replacer, SyncState, Time);
    }

    public TempDir Temp { get; }

    public string GameDir { get; }

    public string ProfileDir => Path.Combine(GameDir, "profile");

    public string SaveDir => Path.Combine(ProfileDir, "A");

    public string BackupsDir { get; }

    public FakeGameProcess Game { get; } = new();

    public TransferGate Gate { get; } = new();

    public SettingsSession Settings { get; }

    public SyncStateStore SyncState { get; }

    public SaveStatusCache Cache { get; }

    public StatusQuery Query { get; }

    public FakeTimeProvider Time { get; }

    public FaultyFileOps Ops { get; }

    public BackupStore Backups { get; }

    public MirrorReplacer Replacer { get; }

    public SaveReceiver Receiver { get; }

    public LeftoverRecovery RealRecovery() => RecoveryWith(new FileOps());

    public LeftoverRecovery RecoveryWith(IFileOps ops) => new(ops, new BackupStore(BackupsDir, new FileOps(), Time), SwapRetry.None);

    public static async Task<ReceiverRig> CreateAsync(bool withSave = true, string? saveDirOverride = null)
    {
        var temp = new TempDir();
        var gameDir = temp.Combine("game");
        SaveTreeBuilder.WriteFile(gameDir, "totk.exe", "exe");
        SaveTreeBuilder.WriteFile(gameDir, "profile/readme.txt", "neighbour file");
        SaveTreeBuilder.WriteFile(gameDir, "profile/B-not-a-save/data.bin", "neighbour dir");
        SaveTreeBuilder.WriteFile(gameDir, "profile/A-backup/option.sav", "neighbour copy");
        var rig = new ReceiverRig(temp, new LauncherSettings(GameDir: gameDir, SaveDir: saveDirOverride));
        if (withSave)
        {
            SeedSave(rig.SaveDir);
        }
        await rig.Cache.RefreshAsync();
        return rig;
    }

    /// <summary>Everything under the game folder except the save itself and our own staging folders: what must never change.</summary>
    public SortedDictionary<string, string> Neighbours()
    {
        var outside = TreeSnapshot.Capture(GameDir)
            .Where(pair => pair.Key != SaveKey && !pair.Key.StartsWith(SaveKey + "/", StringComparison.Ordinal))
            .Where(pair => !pair.Key.Contains(".totksync-", StringComparison.Ordinal));
        return new SortedDictionary<string, string>(outside.ToDictionary(pair => pair.Key, pair => pair.Value), StringComparer.Ordinal);
    }

    public SortedDictionary<string, string> SaveTree() => TreeSnapshot.Capture(SaveDir);

    public void Dispose() => Temp.Dispose();

    public static void SeedSave(string saveDir)
    {
        SaveTreeBuilder.BuildFullSave(saveDir);
        SaveTreeBuilder.WriteFile(saveDir, "slot_00/stale-only-on-pc.sav", "stale");
        SaveTreeBuilder.WriteFile(saveDir, "Thumbs.db", "old junk");
        SaveTreeBuilder.WriteFile(saveDir, "slot_00/desktop.ini", "old nested junk");
        SaveTreeBuilder.WriteFile(saveDir, "album/pictures.zip", "old zip junk");
        Directory.CreateDirectory(Path.Combine(saveDir, "old-empty-dir"));
    }
}
