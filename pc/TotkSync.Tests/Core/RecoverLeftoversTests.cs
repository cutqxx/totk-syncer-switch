using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class RecoverLeftoversTests : IAsyncLifetime
{
    private ReceiverRig _rig = null!;

    public async Task InitializeAsync() => _rig = await ReceiverRig.CreateAsync();

    public Task DisposeAsync()
    {
        _rig.Dispose();
        return Task.CompletedTask;
    }

    private string Sibling(string name) => Path.Combine(_rig.ProfileDir, name);

    [Fact]
    public void Orphaned_old_folder_without_a_save_is_moved_back_with_all_its_content()
    {
        var before = _rig.SaveTree();
        var old = Sibling("A.totksync-old-0a1b2c3d");
        Directory.Move(_rig.SaveDir, old);

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal(before, _rig.SaveTree());
        Assert.False(Directory.Exists(old));
    }

    [Fact]
    public void Old_folder_with_a_matching_backup_is_deleted_without_a_second_backup()
    {
        var before = _rig.SaveTree();
        var old = Sibling("A.totksync-old-0a1b2c3d");
        Directory.Move(_rig.SaveDir, old);
        ReceiverRig.SeedSave(_rig.SaveDir);
        _rig.Backups.CreateBeforeSwitch(old);

        var notes = _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal(before, _rig.SaveTree());
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
        Assert.Single(Directory.GetFiles(_rig.BackupsDir));
        Assert.Contains(notes, note => note.Contains("before-switch.zip", StringComparison.Ordinal));
    }

    [Fact]
    public void Old_folder_without_a_backup_is_backed_up_and_verified_before_it_is_deleted()
    {
        var old = Sibling("A.totksync-old-0a1b2c3d");
        SaveTreeBuilder.BuildMinimalSave(old);
        var oldFingerprint = Fingerprint.OfDirectory(old);

        var notes = _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Contains(notes, note => note.Contains("recovered.zip", StringComparison.Ordinal));
        Assert.False(Directory.Exists(old));
        Assert.NotNull(_rig.Backups.FindBackupOf(oldFingerprint));
        Assert.Matches(@"^pc-\d{8}-\d{6}-recovered\.zip$", Path.GetFileName(Directory.GetFiles(_rig.BackupsDir).Single()));
    }

    [Fact]
    public void Old_folder_that_cannot_be_backed_up_is_kept_and_reported()
    {
        var old = Sibling("A.totksync-old-0a1b2c3d");
        SaveTreeBuilder.BuildMinimalSave(old);
        _rig.Ops.FailWhen = (call, _) => call.Op == FileOp.WriteFile;
        var recovery = new LeftoverRecovery(_rig.Ops, _rig.Backups, SwapRetry.None);

        var notes = recovery.Recover(_rig.ProfileDir);

        Assert.True(Directory.Exists(old));
        Assert.Contains(notes, note => note.Contains(old, StringComparison.Ordinal));
    }

    [Fact]
    public void Orphaned_part_files_are_deleted_and_foreign_files_are_not()
    {
        Directory.CreateDirectory(_rig.BackupsDir);
        var ours = Path.Combine(_rig.BackupsDir, "pc-20261107-103015-before-switch.zip.part");
        var foreign = new[] { "notes.part", "pc-x-before-switch.zip.part", "pc-20261107-103015-before-switch.zip" };
        File.WriteAllText(ours, "x");
        foreign.ToList().ForEach(name => File.WriteAllText(Path.Combine(_rig.BackupsDir, name), "x"));

        _rig.Backups.DeleteOrphanedParts();

        Assert.False(File.Exists(ours));
        Assert.All(foreign, name => Assert.True(File.Exists(Path.Combine(_rig.BackupsDir, name))));
    }

    [Fact]
    public void Stage_folder_is_deleted_and_never_promoted_to_the_save()
    {
        Directory.Move(_rig.SaveDir, Sibling("A.totksync-old-0a1b2c3d"));
        SaveTreeBuilder.WriteFile(Sibling("A.totksync-new-0a1b2c3d"), "option.sav", "half written new save");

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal("option", File.ReadAllText(Path.Combine(_rig.SaveDir, "option.sav")));
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Leftovers_of_another_save_folder_are_resolved_against_their_own_original()
    {
        SaveTreeBuilder.BuildMinimalSave(Sibling("B"));
        var beforeB = TreeSnapshot.Capture(Sibling("B"));
        Directory.Move(Sibling("B"), Sibling("B.totksync-old-ffffffff"));

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal(beforeB, TreeSnapshot.Capture(Sibling("B")));
    }

    [Fact]
    public void Folders_and_files_that_are_not_ours_are_never_touched()
    {
        SaveTreeBuilder.WriteFile(_rig.ProfileDir, "A.old/important.sav", "mine");
        SaveTreeBuilder.WriteFile(_rig.ProfileDir, "A.totksync/important.sav", "mine");
        SaveTreeBuilder.WriteFile(_rig.ProfileDir, "totksync-old/important.sav", "mine");
        SaveTreeBuilder.WriteFile(_rig.ProfileDir, "A.totksync-old-0a1b2c3d.txt", "a file, not a folder");
        SaveTreeBuilder.WriteFile(_rig.ProfileDir, "A.totksync-new-0a1b2c3d", "a file, not a folder");
        Directory.CreateDirectory(Sibling("A.totksync-old-0a1b2c3d"));
        var outside = _rig.Neighbours();

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal(outside, _rig.Neighbours());
        Assert.True(File.Exists(Sibling("A.totksync-new-0a1b2c3d")));
        Assert.True(File.Exists(Sibling("A.totksync-old-0a1b2c3d.txt")));
    }

    [Fact]
    public void A_folder_that_only_contains_the_marker_text_is_not_deleted()
    {
        SaveTreeBuilder.WriteFile(_rig.ProfileDir, "notes.totksync-keep/important.sav", "mine");

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.True(File.Exists(Sibling("notes.totksync-keep/important.sav")));
    }

    [Fact]
    public void Failure_to_list_the_parent_folder_is_reported_as_a_note_and_does_not_throw()
    {
        var recovery = new LeftoverRecovery(new FileOps(), _rig.Backups, SwapRetry.None, _ => throw new UnauthorizedAccessException("access denied"));

        var notes = recovery.Recover(_rig.ProfileDir);

        Assert.Contains(notes, note => note.Contains("access denied", StringComparison.Ordinal));
    }

    [Fact]
    public void A_leftover_that_cannot_be_deleted_does_not_stop_the_recovery_of_the_others()
    {
        SaveTreeBuilder.WriteFile(Sibling("A.totksync-new-0a1b2c3d"), "option.sav", "x");
        SaveTreeBuilder.WriteFile(Sibling("A.totksync-new-1a1b2c3d"), "option.sav", "x");
        _rig.Ops.FailWhen = (call, number) => call.Op == FileOp.DeleteDirectory && number == 1;

        var notes = _rig.RecoveryWith(_rig.Ops).Recover(_rig.ProfileDir);

        Assert.Single(notes);
        Assert.Single(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Missing_parent_folder_is_not_an_error()
    {
        var exception = Record.Exception(() => _rig.RealRecovery().Recover(_rig.Temp.Combine("does-not-exist")));

        Assert.Null(exception);
    }

    [Fact]
    public void Recovery_that_cannot_delete_does_not_throw_and_leaves_the_leftover_for_next_time()
    {
        SaveTreeBuilder.WriteFile(Sibling("A.totksync-new-0a1b2c3d"), "option.sav", "x");
        _rig.Ops.FailWhen = (call, _) => call.Op == FileOp.DeleteDirectory;
        var replacer = _rig.RecoveryWith(_rig.Ops);

        var exception = Record.Exception(() => replacer.Recover(_rig.ProfileDir));

        Assert.Null(exception);
        Assert.True(Directory.Exists(Sibling("A.totksync-new-0a1b2c3d")));
    }

    [Fact]
    public void Recovery_that_cannot_move_the_orphan_back_keeps_it_where_it_is()
    {
        var old = Sibling("A.totksync-old-0a1b2c3d");
        Directory.Move(_rig.SaveDir, old);
        var before = TreeSnapshot.Capture(old);
        _rig.Ops.FailWhen = (call, _) => call.Op == FileOp.MoveDirectory;
        var replacer = _rig.RecoveryWith(_rig.Ops);

        replacer.Recover(_rig.ProfileDir);

        Assert.Equal(before, TreeSnapshot.Capture(old));
        Assert.False(Directory.Exists(_rig.SaveDir));
    }

    [Fact]
    public void Recovery_with_nothing_to_recover_changes_nothing()
    {
        var everything = TreeSnapshot.Capture(_rig.GameDir);

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal(everything, TreeSnapshot.Capture(_rig.GameDir));
    }

    [Fact]
    public void Parent_folder_is_the_profile_folder_of_the_game()
    {
        Assert.Equal(Path.Combine("G", "profile"), SaveLocator.ParentDirectory("G", null));
    }

    [Fact]
    public void Parent_folder_with_a_save_folder_override_is_the_parent_of_the_override()
    {
        var overrideDir = Path.Combine("S", "saves", "A") + Path.DirectorySeparatorChar;

        Assert.Equal(Path.Combine("S", "saves"), SaveLocator.ParentDirectory("G", overrideDir));
    }

    [Fact]
    public void Parent_folder_is_unknown_without_a_game_folder_and_without_an_override()
    {
        Assert.Null(SaveLocator.ParentDirectory(null, null));
        Assert.Null(SaveLocator.ParentDirectory("  ", ""));
    }

    [Fact]
    public void Save_locator_ignores_complete_looking_leftover_copies_next_to_the_save()
    {
        var copy = Sibling("A.totksync-old-0a1b2c3d");
        SaveTreeBuilder.BuildMinimalSave(copy);
        SaveTreeBuilder.BuildMinimalSave(Sibling("A.totksync-new-0a1b2c3d"));

        Assert.Equal(new LocateResult.Found(_rig.SaveDir), SaveLocator.Locate(_rig.GameDir, null));
    }

    [Fact]
    public void Save_locator_reports_not_found_when_only_leftover_copies_exist()
    {
        Directory.Move(_rig.SaveDir, Sibling("A.totksync-old-0a1b2c3d"));

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(_rig.GameDir, null));
    }

    [Fact]
    public void Save_locator_ignores_a_complete_looking_bad_folder_beside_a_full_save()
    {
        SaveTreeBuilder.BuildFullSave(Sibling("A.totksync-bad-0a1b2c3d"));

        Assert.Equal(new LocateResult.Found(_rig.SaveDir), SaveLocator.Locate(_rig.GameDir, null));
    }

    [Fact]
    public void Bad_folder_with_a_matching_backup_is_deleted_without_a_second_backup()
    {
        var bad = Sibling("A.totksync-bad-0a1b2c3d");
        ReceiverRig.SeedSave(bad);
        _rig.Backups.CreateBeforeSwitch(bad);
        var before = _rig.SaveTree();

        var notes = _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.False(Directory.Exists(bad));
        Assert.Single(Directory.GetFiles(_rig.BackupsDir));
        Assert.Equal(before, _rig.SaveTree());
        Assert.Contains(notes, note => note.Contains("before-switch.zip", StringComparison.Ordinal));
    }

    [Fact]
    public void Bad_folder_that_cannot_be_backed_up_is_kept_and_reported()
    {
        var bad = Sibling("A.totksync-bad-0a1b2c3d");
        SaveTreeBuilder.BuildMinimalSave(bad);
        _rig.Ops.FailWhen = (call, _) => call.Op == FileOp.WriteFile;

        var notes = new LeftoverRecovery(_rig.Ops, _rig.Backups, SwapRetry.None).Recover(_rig.ProfileDir);

        Assert.True(Directory.Exists(bad));
        Assert.Contains(notes, note => note.Contains(bad, StringComparison.Ordinal));
    }

    [Fact]
    public void Bad_folder_without_a_backup_is_backed_up_before_it_is_deleted()
    {
        var bad = Sibling("A.totksync-bad-0a1b2c3d");
        SaveTreeBuilder.BuildMinimalSave(bad);
        var badFingerprint = Fingerprint.OfDirectory(bad);

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.False(Directory.Exists(bad));
        Assert.NotNull(_rig.Backups.FindBackupOf(badFingerprint));
    }

    [Fact]
    public void Bad_folder_is_never_deleted_before_the_old_folder_is_back_whatever_the_listing_order()
    {
        var old = Sibling("A.totksync-old-0a1b2c3d");
        var bad = Sibling("A.totksync-bad-0a1b2c3d");
        Directory.Move(_rig.SaveDir, old);
        ReceiverRig.SeedSave(bad);
        File.Delete(Path.Combine(bad, "slot_00", "caption.sav"));
        var before = TreeSnapshot.Capture(old);
        var badFirst = new LeftoverRecovery(new FileOps(), _rig.Backups, SwapRetry.None, dir => Directory.EnumerateDirectories(dir).OrderByDescending(path => path.Contains("-bad-", StringComparison.Ordinal)));

        badFirst.Recover(_rig.ProfileDir);

        Assert.Equal(before, _rig.SaveTree());
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Marker_without_an_old_folder_is_deleted_and_lookalike_files_are_not()
    {
        var orphan = Sibling("A.totksync-old-0a1b2c3d.unverified");
        var lookalikes = new[] { "A.totksync-old-0a1b2c3d.unverified.txt", "A.totksync-new-0a1b2c3d.unverified", "A.unverified", "totksync-old-0a1b2c3d.unverified" };
        File.WriteAllText(orphan, "");
        lookalikes.ToList().ForEach(name => File.WriteAllText(Sibling(name), "mine"));

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.False(File.Exists(orphan));
        Assert.All(lookalikes, name => Assert.True(File.Exists(Sibling(name))));
    }

    [Fact]
    public void Marker_beside_an_existing_old_folder_is_kept_until_that_folder_is_resolved()
    {
        var old = Sibling("A.totksync-old-0a1b2c3d");
        Directory.Move(_rig.SaveDir, old);
        ReceiverRig.SeedSave(_rig.SaveDir);
        File.WriteAllText(StagingNames.UnverifiedMarkerFor(old), "");
        _rig.Ops.FailWhen = (call, _) => call.Op == FileOp.MoveDirectory;

        _rig.RecoveryWith(_rig.Ops).Recover(_rig.ProfileDir);

        Assert.True(Directory.Exists(old));
        Assert.True(File.Exists(StagingNames.UnverifiedMarkerFor(old)));
    }

    [Fact]
    public void Marker_is_deleted_together_with_an_old_folder_that_held_no_save_files()
    {
        var old = Sibling("A.totksync-old-0a1b2c3d");
        SaveTreeBuilder.WriteFile(old, "Thumbs.db", "junk");
        File.WriteAllText(StagingNames.UnverifiedMarkerFor(old), "");
        var before = _rig.SaveTree();

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
        Assert.Equal(before, _rig.SaveTree());
    }

    [Fact]
    public void Marker_is_deleted_after_the_old_folder_is_moved_back()
    {
        var old = Sibling("A.totksync-old-0a1b2c3d");
        Directory.Move(_rig.SaveDir, old);
        File.WriteAllText(StagingNames.UnverifiedMarkerFor(old), "");

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.True(Directory.Exists(_rig.SaveDir));
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }
}
