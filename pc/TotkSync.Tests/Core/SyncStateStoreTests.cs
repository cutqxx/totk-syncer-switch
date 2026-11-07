using System.Text.Json;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SyncStateStoreTests
{
    [Fact]
    public void Missing_state_file_means_never_synced()
    {
        using var temp = new TempDir();

        Assert.Null(new SyncStateStore(temp.Combine("state.json")).Load());
    }

    [Fact]
    public void State_file_with_switch_to_pc_sync_is_read()
    {
        using var temp = new TempDir();
        var path = temp.Combine("state.json");
        File.WriteAllText(path, """
            {"last_sync":{"fingerprint":"sha256:abc","at":"2026-11-03T14:25:00+00:00","direction":"switch->pc"}}
            """);

        var lastSync = new SyncStateStore(path).Load();

        Assert.Equal(
            new LastSync("sha256:abc", new DateTimeOffset(2026, 11, 3, 14, 25, 0, TimeSpan.Zero), SyncDirection.SwitchToPc),
            lastSync);
    }

    [Fact]
    public void State_file_with_pc_to_switch_direction_is_read()
    {
        using var temp = new TempDir();
        var path = temp.Combine("state.json");
        File.WriteAllText(path, """
            {"last_sync":{"fingerprint":"sha256:abc","at":"2026-11-03T14:25:00+00:00","direction":"pc->switch"}}
            """);

        Assert.Equal(SyncDirection.PcToSwitch, new SyncStateStore(path).Load()!.Direction);
    }

    [Fact]
    public void State_file_without_last_sync_means_never_synced()
    {
        using var temp = new TempDir();
        var path = temp.Combine("state.json");
        File.WriteAllText(path, "{}");

        Assert.Null(new SyncStateStore(path).Load());
    }

    [Fact]
    public void Corrupt_state_file_throws_json_exception()
    {
        using var temp = new TempDir();
        var path = temp.Combine("state.json");
        File.WriteAllText(path, "{ nope");

        Assert.ThrowsAny<JsonException>(() => new SyncStateStore(path).Load());
    }

    [Fact]
    public void Unknown_direction_is_treated_as_corrupt_state()
    {
        using var temp = new TempDir();
        var path = temp.Combine("state.json");
        File.WriteAllText(path, """
            {"last_sync":{"fingerprint":"sha256:abc","at":"2026-11-03T14:25:00+00:00","direction":"sideways"}}
            """);

        Assert.ThrowsAny<JsonException>(() => new SyncStateStore(path).Load());
    }
}
