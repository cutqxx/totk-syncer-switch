using TotkSync.Core.Backups;
using TotkSync.Core.Sync;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class BackupRotationTests : IAsyncLifetime
{
    private const string JustCreated = ReceiverRig.ExpectedBackupName;

    private ReceiverRig _rig = null!;

    public async Task InitializeAsync() => _rig = await ReceiverRig.CreateAsync();

    public Task DisposeAsync()
    {
        _rig.Dispose();
        return Task.CompletedTask;
    }

    private static string Name(int day, string kind = "before-switch") => $"pc-202610{day:00}-120000-{kind}.zip";

    private static string FutureName(int day) => $"pc-203010{day:00}-120000-before-switch.zip";

    private string PathOf(string name) => Path.Combine(_rig.BackupsDir, name);

    private void Seed(params string[] names)
    {
        Directory.CreateDirectory(_rig.BackupsDir);
        foreach (var name in names)
        {
            File.WriteAllText(PathOf(name), "old backup " + name);
        }
    }

    private string[] Remaining() => Directory.GetFileSystemEntries(_rig.BackupsDir).Select(Path.GetFileName).Order(StringComparer.Ordinal).ToArray()!;

    private async Task SetKeepAsync(int keep) => Assert.Null(await _rig.Settings.SetKeepBackupsAsync(keep));

    private async Task<ReceiveResult> ReplaceSaveAsync()
    {
        var zip = ZipBuilder.ContractZip("valid-save-tree.zip");
        return await _rig.Receiver.ReceiveAsync(new PushRequest(zip.Length, _ => Task.FromResult(zip), IsForced: true), CancellationToken.None);
    }

    [Fact]
    public async Task Replace_keeps_only_the_newest_backups_up_to_keep_count_including_the_new_one()
    {
        await SetKeepAsync(3);
        Seed(Name(1), Name(2), Name(3), Name(4), Name(5));

        await ReplaceSaveAsync();

        Assert.Equal([Name(4), Name(5), JustCreated], Remaining());
    }

    [Fact]
    public async Task Ordering_follows_the_timestamp_in_the_name_not_the_file_time()
    {
        await SetKeepAsync(2);
        Seed(Name(9), Name(1));
        File.SetLastWriteTimeUtc(PathOf(Name(1)), DateTime.UtcNow.AddDays(1));
        File.SetLastWriteTimeUtc(PathOf(Name(9)), DateTime.UtcNow.AddYears(-3));

        await ReplaceSaveAsync();

        Assert.Equal([Name(9), JustCreated], Remaining());
    }

    [Fact]
    public async Task Default_keep_of_twenty_leaves_twenty_backups_after_a_replace()
    {
        Seed(Enumerable.Range(1, 25).Select(day => Name(day)).ToArray());

        await ReplaceSaveAsync();

        var remaining = Remaining();
        Assert.Equal(20, remaining.Length);
        Assert.Contains(JustCreated, remaining);
        Assert.DoesNotContain(Name(1), remaining);
        Assert.Contains(Name(25), remaining);
    }

    [Fact]
    public async Task Nothing_is_deleted_while_the_backup_count_fits_within_keep()
    {
        await SetKeepAsync(5);
        Seed(Name(1), Name(2));

        await ReplaceSaveAsync();

        Assert.Equal([Name(1), Name(2), JustCreated], Remaining());
    }

    [Fact]
    public async Task The_just_created_backup_survives_even_when_every_other_backup_has_a_newer_name()
    {
        await SetKeepAsync(1);
        Seed(FutureName(20), FutureName(21));

        await ReplaceSaveAsync();

        Assert.Equal([JustCreated], Remaining());
    }

    [Fact]
    public async Task Keep_of_two_with_newer_names_keeps_the_new_backup_and_the_single_newest_old_one()
    {
        await SetKeepAsync(2);
        Seed(FutureName(20), FutureName(21), FutureName(22));

        await ReplaceSaveAsync();

        Assert.Equal([JustCreated, FutureName(22)], Remaining());
    }

    [Fact]
    public async Task Files_that_do_not_match_the_launcher_pattern_are_never_deleted()
    {
        await SetKeepAsync(1);
        string[] foreign =
        [
            "notes.txt",
            "totk-20200101-000000-manual.zip",
            "pc-20200101-000000-before-switch.zip.part",
            "pc-20200101-000000-manual.zip",
            "pc-2020-before-switch.zip",
            "PC-20200101-000000-before-switch.zip",
            "pc-20200101-000000-before-switch.zip.bak",
        ];
        Seed(foreign);
        Seed(Name(1), Name(2));

        await ReplaceSaveAsync();

        Assert.Equal(foreign.Append(JustCreated).Order(StringComparer.Ordinal), Remaining());
    }

    [Fact]
    public async Task A_subfolder_in_the_backups_folder_is_left_alone()
    {
        await SetKeepAsync(1);
        Directory.CreateDirectory(PathOf("pc-20200101-000000-before-switch.zip"));
        Seed(Name(1));

        await ReplaceSaveAsync();

        Assert.True(Directory.Exists(PathOf("pc-20200101-000000-before-switch.zip")));
    }

    [Fact]
    public async Task Recovered_backups_count_toward_the_keep_limit_like_before_switch_ones()
    {
        await SetKeepAsync(2);
        Seed(Name(1, "recovered"), Name(2, "before-switch"), Name(3, "recovered"));

        await ReplaceSaveAsync();

        Assert.Equal([Name(3, "recovered"), JustCreated], Remaining());
    }

    [Fact]
    public async Task A_conflict_never_rotates_the_backups()
    {
        await SetKeepAsync(1);
        Seed(Name(1), Name(2), Name(3));
        var differing = ZipBuilder.ContractZip("valid-save-tree.zip");

        var conflict = await _rig.Receiver.ReceiveAsync(new PushRequest(differing.Length, _ => Task.FromResult(differing)), CancellationToken.None);

        Assert.IsType<ReceiveResult.Conflict>(conflict);
        Assert.Equal([Name(1), Name(2), Name(3)], Remaining());
    }

    [Fact]
    public async Task A_failed_replace_does_not_delete_old_backups()
    {
        await SetKeepAsync(1);
        Seed(Name(1), Name(2));
        _rig.Ops.FailOnNth(FileOp.MoveDirectory, 2);

        var result = await ReplaceSaveAsync();

        Assert.IsType<ReceiveResult.Failed>(result);
        Assert.Contains(Name(1), Remaining());
        Assert.Contains(Name(2), Remaining());
    }

    [Fact]
    public void Prune_deletes_nothing_when_the_folder_does_not_exist()
    {
        _rig.Backups.PruneOldBackups(20, JustCreated);

        Assert.False(Directory.Exists(_rig.BackupsDir));
    }

    [Fact]
    public void Prune_survives_a_backup_that_cannot_be_deleted_and_still_removes_the_others()
    {
        Seed(Name(1), Name(2), Name(3));
        _rig.Ops.FailWhenPath(FileOp.DeleteFile, path => path.EndsWith(Name(2), StringComparison.Ordinal));

        _rig.Backups.PruneOldBackups(1, Name(3));

        Assert.Equal([Name(2), Name(3)], Remaining());
    }

    [Fact]
    public void List_returns_backups_newest_first_with_kind_time_and_size()
    {
        Seed(Name(2, "recovered"), Name(5), Name(3));
        File.WriteAllText(PathOf(Name(5)), "12345");

        var entries = _rig.Backups.List();

        Assert.Equal(
            [
                new BackupEntry(Name(5), new DateTime(2026, 10, 5, 12, 0, 0), "before-switch", 5),
                new BackupEntry(Name(3), new DateTime(2026, 10, 3, 12, 0, 0), "before-switch", "old backup ".Length + Name(3).Length),
                new BackupEntry(Name(2, "recovered"), new DateTime(2026, 10, 2, 12, 0, 0), "recovered", "old backup ".Length + Name(2, "recovered").Length),
            ],
            entries);
    }

    [Fact]
    public void List_ignores_foreign_files_partial_files_and_impossible_dates()
    {
        Seed(Name(1), "notes.txt", "totk-20260101-000000-manual.zip", Name(2) + ".part", "pc-20269999-999999-before-switch.zip");

        var entries = _rig.Backups.List();

        Assert.Equal([Name(1)], entries.Select(entry => entry.Name));
    }

    private static readonly string[] ImpossibleDates =
    [
        "pc-20269999-999999-before-switch.zip",
        "pc-20261301-120000-recovered.zip",
        "pc-20261032-120000-before-switch.zip",
        "pc-20261007-246000-before-switch.zip",
    ];

    [Fact]
    public async Task Names_with_impossible_dates_are_not_counted_toward_keep_and_never_deleted()
    {
        await SetKeepAsync(2);
        Seed(Name(1), Name(2));
        Seed(ImpossibleDates);

        await ReplaceSaveAsync();

        Assert.Equal(ImpossibleDates.Concat([Name(2), JustCreated]).Order(StringComparer.Ordinal), Remaining());
    }

    [Fact]
    public void Prune_leaves_impossible_dates_alone_even_with_keep_one()
    {
        Seed(Name(1));
        Seed(ImpossibleDates);

        _rig.Backups.PruneOldBackups(1, JustCreated);

        Assert.Equal(ImpossibleDates.Order(StringComparer.Ordinal), Remaining());
    }

    [Fact]
    public void List_hides_every_impossible_date()
    {
        Seed(ImpossibleDates);

        Assert.Empty(_rig.Backups.List());
    }

    [Fact]
    public void An_impossible_date_is_neither_a_backup_nor_a_partial_backup()
    {
        Assert.All(ImpossibleDates, name => Assert.False(BackupName.IsBackup(name)));
        Assert.All(ImpossibleDates, name => Assert.False(BackupName.IsPartial(name + ".part")));
    }

    [Fact]
    public void Find_backup_of_skips_an_impossible_date_name()
    {
        var zip = ZipBuilder.ContractZip("valid-save-tree.zip");
        Directory.CreateDirectory(_rig.BackupsDir);
        File.WriteAllBytes(PathOf(ImpossibleDates[0]), zip);

        Assert.Null(_rig.Backups.FindBackupOf(SaveTreeBuilder.ContractFingerprint));
    }

    /// <summary>Denies listing the backups folder to the current user while the scope lives, so enumeration throws.</summary>
    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    private sealed class ListingDenied : IDisposable
    {
        private readonly DirectoryInfo _directory;
        private readonly System.Security.AccessControl.FileSystemAccessRule _rule;

        public ListingDenied(string path)
        {
            _directory = new DirectoryInfo(path);
            _rule = new System.Security.AccessControl.FileSystemAccessRule(
                System.Security.Principal.WindowsIdentity.GetCurrent().User!,
                System.Security.AccessControl.FileSystemRights.ListDirectory,
                System.Security.AccessControl.AccessControlType.Deny);
            var security = _directory.GetAccessControl();
            security.AddAccessRule(_rule);
            _directory.SetAccessControl(security);
        }

        public void Dispose()
        {
            var security = _directory.GetAccessControl();
            security.RemoveAccessRule(_rule);
            _directory.SetAccessControl(security);
        }
    }

    [Fact]
    public void Prune_does_not_throw_when_the_backups_folder_cannot_be_listed()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }
        Seed(Name(1), Name(2));
        using (new ListingDenied(_rig.BackupsDir))
        {
            Assert.ThrowsAny<UnauthorizedAccessException>(() => Directory.GetFiles(_rig.BackupsDir));
            var error = Record.Exception(() => _rig.Backups.PruneOldBackups(1, JustCreated));

            Assert.Null(error);
        }
        Assert.Equal([Name(1), Name(2)], Remaining());
    }

    [Fact]
    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    public async Task A_rotation_that_cannot_list_the_folder_still_reports_replaced_and_records_the_sync()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }
        ListingDenied? denied = null;
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.MoveFile && denied is null)
            {
                denied = new ListingDenied(_rig.BackupsDir);
            }
        };
        try
        {
            var result = await ReplaceSaveAsync();

            Assert.IsType<ReceiveResult.Replaced>(result);
        }
        finally
        {
            denied?.Dispose();
        }
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, TotkSync.Core.Saves.Fingerprint.OfDirectory(_rig.SaveDir));
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, _rig.SyncState.Load()!.Fingerprint);
    }

    [Fact]
    public void List_is_empty_when_the_backups_folder_does_not_exist()
    {
        Assert.Empty(_rig.Backups.List());
    }
}
