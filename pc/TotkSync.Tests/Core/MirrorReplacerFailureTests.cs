using System.Text.RegularExpressions;
using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Zip;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

/// <summary>Named failure points of the replacement, one scenario each (the sweep covers every call; these explain the outcome).</summary>
public sealed class MirrorReplacerFailureTests : IAsyncLifetime
{
    private ReceiverRig _rig = null!;
    private SaveBefore _before = null!;

    public async Task InitializeAsync()
    {
        _rig = await ReceiverRig.CreateAsync();
        _before = SaveInvariants.Capture(_rig);
    }

    public Task DisposeAsync()
    {
        _rig.Dispose();
        return Task.CompletedTask;
    }

    private static ValidatedSaveZip NewSave => ZipBuilder.Unwrap(SaveZipValidator.Validate(ZipBuilder.ContractZip("valid-save-tree.zip")));

    private static bool IsInsideStage(string path) => path.Contains(".totksync-new-", StringComparison.Ordinal);

    private ReplaceResult.Failed ReplaceExpectingFailure()
    {
        return Assert.IsType<ReplaceResult.Failed>(_rig.Replacer.Replace(_rig.SaveDir, NewSave));
    }

    private string OldFolderNamedIn(string reason)
    {
        var match = Regex.Match(reason, @"it is kept at (?<path>.+)$");
        Assert.True(match.Success, $"the error must name where the previous save is kept: {reason}");
        return match.Groups["path"].Value;
    }

    [Fact]
    public void Stage_that_does_not_match_the_zip_is_never_swapped_in()
    {
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.WriteFile && IsInsideStage(call.Path) && Path.GetFileName(call.Path) == "progress.sav")
            {
                File.WriteAllText(call.Path, "silently corrupted on disk");
            }
        };

        var failed = ReplaceExpectingFailure();

        Assert.Contains("doesn't match the ZIP", failed.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, _before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
        Assert.Equal(0, _rig.Ops.CountOf(FileOp.MoveDirectory));
    }

    [Fact]
    public void Failure_to_create_the_stage_folder_leaves_everything_untouched()
    {
        _rig.Ops.FailOnNth(FileOp.CreateDirectory, 1);

        var failed = ReplaceExpectingFailure();

        Assert.StartsWith("couldn't prepare the new save", failed.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, _before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Failure_when_copying_old_junk_into_the_stage_leaves_the_save_untouched_and_cleans_the_stage()
    {
        _rig.Ops.FailOnNth(FileOp.CopyFile, 2);

        var failed = ReplaceExpectingFailure();

        Assert.StartsWith("couldn't prepare the new save", failed.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, _before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Failure_to_move_the_current_save_aside_leaves_it_in_place_and_cleans_the_stage()
    {
        _rig.Ops.FailOnNth(FileOp.MoveDirectory, 1);

        var failed = ReplaceExpectingFailure();

        Assert.StartsWith("couldn't move the current save aside", failed.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, _before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Failure_to_move_the_stage_into_place_restores_the_previous_save_and_cleans_up()
    {
        _rig.Ops.FailOnNth(FileOp.MoveDirectory, 2);

        var failed = ReplaceExpectingFailure();

        Assert.StartsWith("couldn't install the new save", failed.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, _before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Installed_save_that_fails_verification_is_rolled_back_to_the_previous_one()
    {
        var isDamaged = false;
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.MoveDirectory && call.Target == _rig.SaveDir && !isDamaged)
            {
                isDamaged = true;
                File.Delete(Path.Combine(_rig.SaveDir, "option.sav"));
            }
        };

        var failed = ReplaceExpectingFailure();

        Assert.Contains("doesn't match the ZIP", failed.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, _before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Failure_to_delete_the_old_folder_after_success_still_reports_success_and_recovery_removes_it()
    {
        _rig.Ops.FailOnNth(FileOp.DeleteDirectory, 1);

        var result = _rig.Replacer.Replace(_rig.SaveDir, NewSave);

        Assert.IsType<ReplaceResult.Replaced>(result);
        SaveInvariants.AssertSaveIsExactlyTheZip(_rig, _before, NewSave);
        var leftovers = TreeSnapshot.Leftovers(_rig.GameDir);
        Assert.Single(leftovers);
        Assert.Contains(".totksync-old-", leftovers[0]);
        SaveInvariants.AssertRecoveryCleansUpWithoutChangingTheSave(_rig);
        SaveInvariants.AssertSaveIsExactlyTheZip(_rig, _before, NewSave);
    }

    [Fact]
    public void Stage_that_cannot_be_deleted_after_a_failure_is_left_aside_and_ignored_by_the_save_locator_until_recovery()
    {
        _rig.Ops.FailWhen = (call, number) => (call.Op == FileOp.WriteFile && number == 2) || call.Op == FileOp.DeleteDirectory;

        ReplaceExpectingFailure();

        SaveInvariants.AssertSaveIsUntouched(_rig, _before);
        Assert.Single(TreeSnapshot.Leftovers(_rig.GameDir));
        Assert.Equal(new LocateResult.Found(_rig.SaveDir), SaveLocator.Locate(_rig.GameDir, null));
        SaveInvariants.AssertRecoveryCleansUpWithoutChangingTheSave(_rig);
    }

    [Fact]
    public void When_installing_and_rolling_back_both_fail_the_error_names_the_folder_with_the_previous_save()
    {
        _rig.Ops.FailFromNth(FileOp.MoveDirectory, 2);

        var failed = ReplaceExpectingFailure();

        var oldFolder = OldFolderNamedIn(failed.Reason);
        Assert.False(Directory.Exists(_rig.SaveDir));
        Assert.Equal(_before.SaveTree, TreeSnapshot.Capture(oldFolder));
        Assert.True(StagingNames.IsOld(oldFolder));
    }

    [Fact]
    public void Recovery_after_a_failed_rollback_brings_the_previous_save_back_and_removes_the_stage()
    {
        _rig.Ops.FailFromNth(FileOp.MoveDirectory, 2);
        ReplaceExpectingFailure();

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        SaveInvariants.AssertSaveIsUntouched(_rig, _before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void While_the_save_is_orphaned_the_locator_reports_no_save_instead_of_picking_the_old_copy()
    {
        _rig.Ops.FailFromNth(FileOp.MoveDirectory, 2);
        ReplaceExpectingFailure();

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(_rig.GameDir, null));
    }

    [Fact]
    public void When_the_rollback_cannot_move_the_installed_save_back_the_previous_save_is_kept_and_named_in_the_error()
    {
        _rig.Ops.FailWhen = (call, number) => call.Op == FileOp.MoveDirectory && number >= 3;
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.MoveDirectory && call.Target == _rig.SaveDir)
            {
                File.Delete(Path.Combine(_rig.SaveDir, "option.sav"));
            }
        };

        var failed = ReplaceExpectingFailure();

        var oldFolder = OldFolderNamedIn(failed.Reason);
        Assert.Equal(_before.SaveTree, TreeSnapshot.Capture(oldFolder));
    }

    [Fact]
    public void Recovery_after_a_failed_rollback_of_an_unverified_install_must_not_destroy_the_previous_save()
    {
        _rig.Ops.FailWhen = (call, number) => call.Op == FileOp.MoveDirectory && number >= 3;
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.MoveDirectory && call.Target == _rig.SaveDir)
            {
                File.Delete(Path.Combine(_rig.SaveDir, "option.sav"));
            }
        };
        var failed = ReplaceExpectingFailure();
        var oldFolder = OldFolderNamedIn(failed.Reason);

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        var previousSaveIsStillReachable = Directory.Exists(oldFolder)
            ? TreeSnapshot.Capture(oldFolder).SequenceEqual(_before.SaveTree)
            : _rig.SaveTree().SequenceEqual(_before.SaveTree);
        Assert.True(previousSaveIsStillReachable, "the previous save was destroyed by recovery");
    }

    private void MakeTripleFailure(Action<string> damageInstalledSave)
    {
        _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);
        _rig.Ops.FailWhen = (call, number) =>
            (call.Op == FileOp.MoveDirectory && number >= 3)
            || (call.Op == FileOp.DeleteDirectory && call.Path == _rig.SaveDir);
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.MoveDirectory && call.Target == _rig.SaveDir)
            {
                damageInstalledSave(_rig.SaveDir);
            }
        };
    }

    [Fact]
    public void Triple_failure_with_a_partly_deleted_installed_save_is_repaired_by_recovery_and_reported()
    {
        MakeTripleFailure(save => File.Delete(Path.Combine(save, "option.sav")));
        var failed = ReplaceExpectingFailure();
        var oldFolder = OldFolderNamedIn(failed.Reason);

        var notes = _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal(_before.SaveFingerprint, Fingerprint.OfDirectory(_rig.SaveDir));
        Assert.False(Directory.Exists(oldFolder));
        Assert.Contains(notes, note => note.Contains("restored the previous save", StringComparison.Ordinal));
        Assert.Contains(notes, note => note.Contains(oldFolder, StringComparison.Ordinal));
    }

    [Fact]
    public void Triple_failure_with_a_complete_looking_but_damaged_installed_save_is_repaired_through_the_marker()
    {
        MakeTripleFailure(save => File.Delete(Path.Combine(save, "slot_00", "caption.sav")));
        ReplaceExpectingFailure();

        var notes = _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal(_before.SaveFingerprint, Fingerprint.OfDirectory(_rig.SaveDir));
        Assert.NotEmpty(notes);
        Assert.Empty(Directory.GetFiles(_rig.ProfileDir, "*.unverified"));
    }

    [Fact]
    public void Triple_failure_keeps_the_unverified_copy_in_a_backup_or_beside_the_save_never_nowhere()
    {
        MakeTripleFailure(save => File.Delete(Path.Combine(save, "slot_00", "caption.sav")));
        ReplaceExpectingFailure();

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.True(_rig.SaveTree().SequenceEqual(_before.SaveTree));
        Assert.DoesNotContain(TreeSnapshot.Leftovers(_rig.Temp.Path), path => path.Contains(".totksync-old-", StringComparison.Ordinal));
    }

    private void AssertLocateNeverReturnsALeftover()
    {
        var located = SaveLocator.Locate(_rig.GameDir, null);
        var dirs = located switch
        {
            LocateResult.Found found => [found.Dir],
            LocateResult.Several several => several.Items.Select(item => item.Dir).ToList(),
            _ => new List<string>(),
        };
        Assert.DoesNotContain(dirs, StagingNames.IsLeftover);
    }

    [Fact]
    public void Triple_failure_when_recovery_cannot_restore_at_first_start_is_repaired_at_the_second_without_loss()
    {
        MakeTripleFailure(save => File.Delete(Path.Combine(save, "slot_00", "caption.sav")));
        ReplaceExpectingFailure();
        _rig.Ops.FailWhen = (call, _) => call.Op == FileOp.MoveDirectory && call.Target == _rig.SaveDir;

        var firstNotes = _rig.RecoveryWith(_rig.Ops).Recover(_rig.ProfileDir);
        AssertLocateNeverReturnsALeftover();
        var secondNotes = _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Contains(firstNotes, note => note.Contains("Couldn't put the previous save", StringComparison.Ordinal));
        Assert.NotEmpty(secondNotes);
        Assert.Equal(new LocateResult.Found(_rig.SaveDir), SaveLocator.Locate(_rig.GameDir, null));
        Assert.Equal(_before.SaveFingerprint, Fingerprint.OfDirectory(_rig.SaveDir));
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
        Assert.NotNull(_rig.Backups.FindBackupOf(_before.SaveFingerprint));
    }

    [Fact]
    public void Recovery_start_with_bad_and_old_folders_never_lets_the_locator_return_a_leftover()
    {
        MakeTripleFailure(save => File.Delete(Path.Combine(save, "slot_00", "caption.sav")));
        ReplaceExpectingFailure();
        _rig.Ops.FailWhen = (call, _) => call.Op == FileOp.MoveDirectory && call.Target == _rig.SaveDir;
        _rig.RecoveryWith(_rig.Ops).Recover(_rig.ProfileDir);

        Assert.Contains(TreeSnapshot.Leftovers(_rig.GameDir), path => path.Contains(".totksync-bad-", StringComparison.Ordinal));
        AssertLocateNeverReturnsALeftover();
    }

    [Fact]
    public void Failed_rollback_after_the_installed_save_was_deleted_leaves_no_marker_after_recovery()
    {
        _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);
        _rig.Ops.FailWhen = (call, number) => call.Op == FileOp.MoveDirectory && number >= 3;
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.MoveDirectory && call.Target == _rig.SaveDir)
            {
                File.Delete(Path.Combine(_rig.SaveDir, "slot_00", "caption.sav"));
            }
        };
        SaveTreeBuilder.WriteFile(_rig.ProfileDir, "A.totksync-old-0a1b2c3d.unverified.txt", "foreign");
        ReplaceExpectingFailure();
        Assert.Single(Directory.GetFiles(_rig.ProfileDir, "*.unverified"));
        Assert.False(Directory.Exists(_rig.SaveDir));

        _rig.RealRecovery().Recover(_rig.ProfileDir);

        Assert.Equal(_before.SaveFingerprint, Fingerprint.OfDirectory(_rig.SaveDir));
        Assert.Empty(Directory.GetFiles(_rig.ProfileDir, "*.unverified"));
        Assert.True(File.Exists(Path.Combine(_rig.ProfileDir, "A.totksync-old-0a1b2c3d.unverified.txt")));
        Assert.True(File.Exists(Path.Combine(_rig.ProfileDir, "readme.txt")));
        AssertLocateNeverReturnsALeftover();
    }
}
