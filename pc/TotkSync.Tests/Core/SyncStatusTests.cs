using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Saves;
using TotkSync.Core.Sync;

namespace TotkSync.Tests.Core;

public class SyncStatusTests
{
    private readonly FakeTimeProvider _clock = new(new DateTimeOffset(2026, 11, 3, 12, 0, 0, TimeSpan.Zero));

    private static SaveSnapshot SnapshotWith(string fingerprint)
    {
        return new SaveSnapshot(@"C:\save", DateTimeOffset.UnixEpoch, 3, 100, fingerprint);
    }

    private LastSync LastSyncWith(string fingerprint)
    {
        return new LastSync(fingerprint, _clock.GetUtcNow(), SyncDirection.SwitchToPc);
    }

    [Fact]
    public void No_last_sync_is_never_synced()
    {
        Assert.Equal(SyncStatus.NeverSynced, SyncStatusEvaluator.Evaluate(SnapshotWith("sha256:a"), null));
    }

    [Fact]
    public void Matching_fingerprint_is_in_sync()
    {
        var status = SyncStatusEvaluator.Evaluate(SnapshotWith("sha256:a"), LastSyncWith("sha256:a"));

        Assert.Equal(SyncStatus.InSync, status);
    }

    [Fact]
    public void Different_fingerprint_means_pc_has_new_progress()
    {
        var status = SyncStatusEvaluator.Evaluate(SnapshotWith("sha256:b"), LastSyncWith("sha256:a"));

        Assert.Equal(SyncStatus.PcHasNewProgress, status);
    }

    [Fact]
    public void Status_does_not_depend_on_how_old_the_last_sync_is()
    {
        var lastSync = LastSyncWith("sha256:a");
        _clock.Advance(TimeSpan.FromDays(400));

        Assert.Equal(SyncStatus.InSync, SyncStatusEvaluator.Evaluate(SnapshotWith("sha256:a"), lastSync));
    }
}
