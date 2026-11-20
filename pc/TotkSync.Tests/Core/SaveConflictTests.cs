using System.Text.Json;
using TotkSync.Core.Saves;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Core.Zip;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class SaveConflictTests : IAsyncLifetime
{
    private static readonly DateTimeOffset EarlierSync = new(2026, 11, 3, 12, 0, 0, TimeSpan.Zero);
    private static readonly string UnrelatedFingerprint = "sha256:" + new string('a', 64);

    private ReceiverRig _rig = null!;

    public async Task InitializeAsync() => _rig = await ReceiverRig.CreateAsync();

    public Task DisposeAsync()
    {
        _rig.Dispose();
        return Task.CompletedTask;
    }

    private static byte[] DifferentZip => ZipBuilder.ContractZip("valid-save-tree.zip");

    private byte[] CurrentSaveAsZip() => SaveZipPacker.Pack(_rig.SaveDir);

    private string StatePath => _rig.Temp.Combine("state", "state.json");

    private string StateBlockerPath => StatePath + ".tmp";

    private string CurrentFingerprint() => Fingerprint.OfDirectory(_rig.SaveDir);

    private Task<ReceiveResult> PushAsync(byte[] zip, bool isForced = false)
    {
        var request = new PushRequest(zip.Length, _ => Task.FromResult(zip), isForced);
        return _rig.Receiver.ReceiveAsync(request, CancellationToken.None);
    }

    private void SeedLastSync(string fingerprint, SyncDirection direction = SyncDirection.PcToSwitch)
    {
        _rig.SyncState.Save(new LastSync(fingerprint, EarlierSync, direction));
    }

    private string[] BackupFiles() => Directory.Exists(_rig.BackupsDir) ? Directory.GetFiles(_rig.BackupsDir) : [];

    private DateTimeOffset PcModified() => Assert.IsType<SaveStatus.Found>(_rig.Cache.Current).Snapshot.Modified;

    private void WriteStateFile(string text)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(StatePath)!);
        File.WriteAllText(StatePath, text);
    }

    /// <summary>A directory squatting on the temp name makes every state.json write fail with an I/O error.</summary>
    private void BlockStateWrites() => Directory.CreateDirectory(StateBlockerPath);

    private void UnblockStateWrites() => Directory.Delete(StateBlockerPath);

    [Fact]
    public async Task Differing_zip_is_a_conflict_when_the_pc_save_changed_since_the_last_sync()
    {
        SeedLastSync(UnrelatedFingerprint);
        var pcModified = PcModified();

        var result = await PushAsync(DifferentZip);

        Assert.Equal(new ReceiveResult.Conflict(pcModified, EarlierSync), result);
    }

    [Fact]
    public async Task Conflict_changes_nothing_no_save_files_no_backup_and_the_old_sync_state()
    {
        SeedLastSync(UnrelatedFingerprint);
        var before = SaveInvariants.Capture(_rig);
        var stateBefore = File.ReadAllText(StatePath);

        await PushAsync(DifferentZip);

        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Empty(BackupFiles());
        Assert.Equal(stateBefore, File.ReadAllText(StatePath));
    }

    [Fact]
    public async Task Without_sync_history_a_differing_zip_is_a_conflict_with_no_last_sync_date()
    {
        var pcModified = PcModified();

        var result = await PushAsync(DifferentZip);

        Assert.Equal(new ReceiveResult.Conflict(pcModified, null), result);
        Assert.False(File.Exists(StatePath));
        Assert.Empty(BackupFiles());
    }

    [Fact]
    public async Task Corrupt_state_file_counts_as_no_history_and_gives_a_conflict()
    {
        WriteStateFile("{ this is not json");

        var result = await PushAsync(DifferentZip);

        Assert.Equal(new ReceiveResult.Conflict(PcModified(), null), result);
    }

    [Fact]
    public async Task State_file_with_an_unknown_direction_counts_as_no_history()
    {
        WriteStateFile("""{"last_sync":{"fingerprint":"sha256:x","at":"2026-11-03T12:00:00+00:00","direction":"sideways"}}""");

        var result = await PushAsync(DifferentZip);

        Assert.Equal(new ReceiveResult.Conflict(PcModified(), null), result);
    }

    [Fact]
    public async Task Corrupt_state_file_does_not_stop_a_forced_push()
    {
        WriteStateFile("garbage");

        var result = await PushAsync(DifferentZip, isForced: true);

        Assert.IsType<ReceiveResult.Replaced>(result);
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, _rig.SyncState.Load()!.Fingerprint);
    }

    [Fact]
    public async Task Differing_zip_without_force_replaces_when_the_pc_save_is_the_one_last_synced()
    {
        SeedLastSync(CurrentFingerprint());

        var result = await PushAsync(DifferentZip);

        Assert.IsType<ReceiveResult.Replaced>(result);
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, CurrentFingerprint());
    }

    [Fact]
    public async Task Force_replaces_the_save_even_in_a_conflict()
    {
        SeedLastSync(UnrelatedFingerprint);

        var result = await PushAsync(DifferentZip, isForced: true);

        var replaced = Assert.IsType<ReceiveResult.Replaced>(result);
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, replaced.Fingerprint);
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, CurrentFingerprint());
    }

    [Fact]
    public async Task Forced_replace_backs_up_the_conflicting_pc_save_first()
    {
        var pcSaveBefore = CurrentFingerprint();

        await PushAsync(DifferentZip, isForced: true);

        var backup = Assert.Single(BackupFiles());
        Assert.Equal(ReceiverRig.ExpectedBackupName, Path.GetFileName(backup));
        SaveInvariants.AssertEveryBackupIsACompleteCopyOf(_rig, pcSaveBefore);
    }

    [Fact]
    public async Task Successful_replace_records_fingerprint_time_and_switch_to_pc_direction()
    {
        await PushAsync(DifferentZip, isForced: true);

        Assert.Equal(
            new LastSync(SaveTreeBuilder.ContractFingerprint, ReceiverRig.FakeNow, SyncDirection.SwitchToPc),
            _rig.SyncState.Load());
    }

    [Fact]
    public async Task Sync_state_is_written_to_state_json_in_the_documented_shape()
    {
        await PushAsync(DifferentZip, isForced: true);

        var lastSync = JsonDocument.Parse(File.ReadAllText(StatePath)).RootElement.GetProperty("last_sync");
        Assert.Equal(
            (SaveTreeBuilder.ContractFingerprint, "switch->pc"),
            (lastSync.GetProperty("fingerprint").GetString(), lastSync.GetProperty("direction").GetString()));
    }

    [Fact]
    public async Task Status_cache_shows_the_new_sync_state_after_a_replace()
    {
        await PushAsync(DifferentZip, isForced: true);

        var found = Assert.IsType<SaveStatus.Found>(_rig.Cache.Current);
        Assert.Equal(
            new LastSync(SaveTreeBuilder.ContractFingerprint, ReceiverRig.FakeNow, SyncDirection.SwitchToPc),
            found.LastSync);
    }

    [Fact]
    public async Task Identical_zip_gives_unchanged_with_the_fingerprint()
    {
        var result = await PushAsync(CurrentSaveAsZip());

        Assert.Equal(new ReceiveResult.Unchanged(CurrentFingerprint()), result);
    }

    [Fact]
    public async Task Unchanged_creates_no_backup_and_performs_no_file_operation_on_the_save()
    {
        var before = SaveInvariants.Capture(_rig);

        await PushAsync(CurrentSaveAsZip());

        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Empty(BackupFiles());
        Assert.Empty(_rig.Ops.Calls);
    }

    [Fact]
    public async Task Unchanged_records_last_sync_even_with_no_history_and_does_not_conflict()
    {
        var result = await PushAsync(CurrentSaveAsZip());

        Assert.IsType<ReceiveResult.Unchanged>(result);
        Assert.Equal(
            new LastSync(CurrentFingerprint(), ReceiverRig.FakeNow, SyncDirection.SwitchToPc),
            _rig.SyncState.Load());
    }

    [Fact]
    public async Task Unchanged_is_decided_before_the_conflict_when_history_points_at_another_save()
    {
        SeedLastSync(UnrelatedFingerprint, SyncDirection.PcToSwitch);

        var result = await PushAsync(CurrentSaveAsZip());

        Assert.IsType<ReceiveResult.Unchanged>(result);
        Assert.Equal(SyncDirection.SwitchToPc, _rig.SyncState.Load()!.Direction);
    }

    [Fact]
    public async Task Unchanged_wins_over_force_so_no_backup_is_made()
    {
        var result = await PushAsync(CurrentSaveAsZip(), isForced: true);

        Assert.IsType<ReceiveResult.Unchanged>(result);
        Assert.Empty(BackupFiles());
    }

    [Fact]
    public async Task Retrying_a_replace_whose_response_was_lost_gives_unchanged_without_a_second_backup()
    {
        await PushAsync(DifferentZip, isForced: true);

        var retry = await PushAsync(DifferentZip);

        Assert.Equal(new ReceiveResult.Unchanged(SaveTreeBuilder.ContractFingerprint), retry);
        Assert.Single(BackupFiles());
    }

    [Fact]
    public async Task Unchanged_refreshes_the_status_cache_with_the_new_last_sync()
    {
        await PushAsync(CurrentSaveAsZip());

        var found = Assert.IsType<SaveStatus.Found>(_rig.Cache.Current);
        Assert.Equal(SyncDirection.SwitchToPc, found.LastSync!.Direction);
    }

    [Fact]
    public async Task Unchanged_with_an_unwritable_state_file_fails_and_changes_no_save_file()
    {
        var before = SaveInvariants.Capture(_rig);
        BlockStateWrites();

        var result = await PushAsync(CurrentSaveAsZip());

        var failed = Assert.IsType<ReceiveResult.Failed>(result);
        Assert.Contains("sync state was not saved", failed.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
    }

    [Fact]
    public async Task State_write_failure_after_a_replace_reports_the_backup_name_while_the_save_is_already_replaced()
    {
        BlockStateWrites();

        var result = await PushAsync(DifferentZip, isForced: true);

        var failed = Assert.IsType<ReceiveResult.Failed>(result);
        Assert.Contains(ReceiverRig.ExpectedBackupName, failed.Reason);
        Assert.Contains("sync state was not saved", failed.Reason);
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, CurrentFingerprint());
        Assert.True(failed.IsSaveChanged);
    }

    [Fact]
    public async Task State_write_failure_after_a_replace_still_publishes_the_new_fingerprint_with_no_last_sync()
    {
        BlockStateWrites();
        var changes = 0;
        _rig.Cache.Changed += () => changes++;

        await PushAsync(DifferentZip, isForced: true);

        var found = Assert.IsType<SaveStatus.Found>(_rig.Cache.Current);
        Assert.Equal((SaveTreeBuilder.ContractFingerprint, null, 1), (found.Snapshot.Fingerprint, found.LastSync, changes));
    }

    [Fact]
    public async Task State_write_failure_after_a_replace_keeps_the_older_last_sync_in_the_cache()
    {
        var oldFingerprint = CurrentFingerprint();
        SeedLastSync(oldFingerprint);
        await _rig.Cache.RefreshAsync();
        BlockStateWrites();

        await PushAsync(DifferentZip);

        var found = Assert.IsType<SaveStatus.Found>(_rig.Cache.Current);
        Assert.Equal((SaveTreeBuilder.ContractFingerprint, oldFingerprint), (found.Snapshot.Fingerprint, found.LastSync!.Fingerprint));
    }

    [Fact]
    public async Task Failure_that_leaves_the_save_untouched_is_not_marked_as_changed_and_does_not_refresh_the_cache()
    {
        var oldFingerprint = CurrentFingerprint();
        var changes = 0;
        _rig.Cache.Changed += () => changes++;
        _rig.Ops.FailOnNth(FileOp.WriteFile, 1);

        var result = await PushAsync(DifferentZip, isForced: true);

        Assert.False(Assert.IsType<ReceiveResult.Failed>(result).IsSaveChanged);
        Assert.Equal((oldFingerprint, 0), (Assert.IsType<SaveStatus.Found>(_rig.Cache.Current).Snapshot.Fingerprint, changes));
    }

    [Fact]
    public async Task Unchanged_with_an_unwritable_state_file_is_not_marked_as_changed()
    {
        BlockStateWrites();

        var result = await PushAsync(CurrentSaveAsZip());

        Assert.False(Assert.IsType<ReceiveResult.Failed>(result).IsSaveChanged);
    }

    [Fact]
    public async Task A_locked_state_file_counts_as_no_history_and_gives_a_conflict()
    {
        SeedLastSync(CurrentFingerprint());
        using var hold = new FileStream(StatePath, FileMode.Open, FileAccess.Read, FileShare.None);

        var result = await PushAsync(DifferentZip);

        Assert.Equal(new ReceiveResult.Conflict(PcModified(), null), result);
    }

    [Fact]
    public async Task Retry_after_a_state_write_failure_gives_unchanged_and_records_the_sync()
    {
        BlockStateWrites();
        await PushAsync(DifferentZip, isForced: true);
        UnblockStateWrites();

        var retry = await PushAsync(DifferentZip);

        Assert.Equal(new ReceiveResult.Unchanged(SaveTreeBuilder.ContractFingerprint), retry);
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, _rig.SyncState.Load()!.Fingerprint);
        Assert.Single(BackupFiles());
    }
}
