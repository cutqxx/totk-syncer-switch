using TotkSync.Core.Saves;
using TotkSync.Core.Zip;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class BackupStoreTests : IAsyncLifetime
{
    private ReceiverRig _rig = null!;

    public async Task InitializeAsync() => _rig = await ReceiverRig.CreateAsync();

    public Task DisposeAsync()
    {
        _rig.Dispose();
        return Task.CompletedTask;
    }

    private static string FingerprintOfBackup(string path)
    {
        return Fingerprint.OfZip(ZipBuilder.Unwrap(SaveZipValidator.Validate(File.ReadAllBytes(path))));
    }

    private string[] BackupFiles() => Directory.Exists(_rig.BackupsDir) ? Directory.GetFiles(_rig.BackupsDir) : [];

    [Fact]
    public void Backup_is_named_after_the_local_time_and_lives_in_the_backups_folder()
    {
        var backup = _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);

        Assert.Equal((ReceiverRig.ExpectedBackupName, Path.Combine(_rig.BackupsDir, ReceiverRig.ExpectedBackupName)), (backup.Name, backup.Path));
        Assert.True(File.Exists(backup.Path));
    }

    [Fact]
    public void Backup_unpacks_to_exactly_the_save_it_was_taken_from()
    {
        var before = Fingerprint.OfDirectory(_rig.SaveDir);

        var backup = _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);

        Assert.Equal(before, FingerprintOfBackup(backup.Path));
    }

    [Fact]
    public void Backup_keeps_empty_save_directories_and_leaves_junk_out()
    {
        var backup = _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);

        var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(File.ReadAllBytes(backup.Path)));

        Assert.Contains("album", zip.Directories);
        Assert.Contains("old-empty-dir", zip.Directories);
        Assert.DoesNotContain(zip.Files, file => JunkFilter.IsJunk(file.RelPath));
    }

    [Fact]
    public void Taking_a_backup_changes_nothing_in_the_save_and_leaves_no_part_file()
    {
        var saveBefore = _rig.SaveTree();

        _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);

        Assert.Equal(saveBefore, _rig.SaveTree());
        Assert.DoesNotContain(BackupFiles(), path => path.EndsWith(".part"));
    }

    [Fact]
    public void Missing_backups_folder_is_created()
    {
        Assert.False(Directory.Exists(_rig.BackupsDir));

        _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);

        Assert.Single(BackupFiles());
    }

    [Fact]
    public void Second_backup_in_the_same_second_gets_the_next_second_and_keeps_the_first_untouched()
    {
        var first = _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);
        var firstBytes = File.ReadAllBytes(first.Path);

        var second = _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);

        Assert.Equal("pc-20261107-103016-before-switch.zip", second.Name);
        Assert.Equal(firstBytes, File.ReadAllBytes(first.Path));
    }

    [Fact]
    public void Foreign_files_in_the_backups_folder_are_never_touched()
    {
        SaveTreeBuilder.WriteFile(_rig.BackupsDir, "my-own-notes.txt", "keep");
        SaveTreeBuilder.WriteFile(_rig.BackupsDir, "pc-20200101-000000-before-switch.zip", "an older backup");

        _rig.Backups.CreateBeforeSwitch(_rig.SaveDir);

        Assert.Equal("keep", File.ReadAllText(Path.Combine(_rig.BackupsDir, "my-own-notes.txt")));
        Assert.Equal("an older backup", File.ReadAllText(Path.Combine(_rig.BackupsDir, "pc-20200101-000000-before-switch.zip")));
    }

    [Theory]
    [InlineData(FileOp.CreateDirectory)]
    [InlineData(FileOp.WriteFile)]
    [InlineData(FileOp.MoveFile)]
    public void Failure_of_any_backup_step_throws_and_leaves_no_backup_and_no_part_file(FileOp failing)
    {
        _rig.Ops.FailOnNth(failing, 1);
        var saveBefore = _rig.SaveTree();

        var error = Assert.Throws<IOException>(() => _rig.Backups.CreateBeforeSwitch(_rig.SaveDir));

        Assert.Contains("injected failure", error.Message);
        Assert.Empty(BackupFiles());
        Assert.Equal(saveBefore, _rig.SaveTree());
    }

    [Fact]
    public void Write_that_fails_after_the_data_reached_the_disk_still_removes_the_part_file()
    {
        _rig.Ops.FailOnNth(FileOp.WriteFile, 1, FaultTiming.AfterOperation);

        Assert.Throws<IOException>(() => _rig.Backups.CreateBeforeSwitch(_rig.SaveDir));

        Assert.Empty(BackupFiles());
    }

    [Fact]
    public void Backup_that_was_silently_truncated_on_disk_is_rejected_and_removed()
    {
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.WriteFile)
            {
                File.WriteAllBytes(call.Path, File.ReadAllBytes(call.Path)[..100]);
            }
        };

        var error = Assert.Throws<IOException>(() => _rig.Backups.CreateBeforeSwitch(_rig.SaveDir));

        Assert.Equal("the backup doesn't match the current save", error.Message);
        Assert.Empty(BackupFiles());
    }

    [Fact]
    public void Backup_that_differs_from_the_save_because_the_save_changed_meanwhile_is_rejected()
    {
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.WriteFile)
            {
                File.WriteAllText(Path.Combine(_rig.SaveDir, "option.sav"), "changed by the game");
            }
        };

        Assert.Throws<IOException>(() => _rig.Backups.CreateBeforeSwitch(_rig.SaveDir));

        Assert.Empty(BackupFiles());
    }

    [Fact]
    public void Unreadable_save_file_makes_packing_fail_before_anything_is_written()
    {
        using var lockedFile = new FileStream(Path.Combine(_rig.SaveDir, "option.sav"), FileMode.Open, FileAccess.Read, FileShare.None);

        Assert.Throws<IOException>(() => _rig.Backups.CreateBeforeSwitch(_rig.SaveDir));

        Assert.Empty(BackupFiles());
        Assert.Empty(_rig.Ops.Calls);
    }

    [Fact]
    public void When_the_part_file_cannot_be_deleted_either_the_original_failure_is_reported()
    {
        _rig.Ops.FailWhen = (call, _) => call.Op is FileOp.MoveFile or FileOp.DeleteFile;

        var error = Assert.Throws<IOException>(() => _rig.Backups.CreateBeforeSwitch(_rig.SaveDir));

        Assert.Contains("MoveFile", error.Message);
    }
}
