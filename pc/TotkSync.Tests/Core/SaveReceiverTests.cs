using TotkSync.Core.Saves;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;
using TotkSync.Core.Zip;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class SaveReceiverTests : IAsyncLifetime
{
    private const long MaxZipBytes = 64L * 1024 * 1024;

    private ReceiverRig _rig = null!;

    public async Task InitializeAsync() => _rig = await ReceiverRig.CreateAsync();

    public Task DisposeAsync()
    {
        _rig.Dispose();
        return Task.CompletedTask;
    }

    private static byte[] ValidZip => ZipBuilder.ContractZip("valid-save-tree.zip");

    private sealed class Body(byte[] bytes)
    {
        public int Reads { get; private set; }

        public Task<byte[]> ReadAsync(CancellationToken ct)
        {
            Reads++;
            return Task.FromResult(bytes);
        }

        public PushRequest Request() => new(bytes.Length, ReadAsync, IsForced: true);

        public PushRequest Request(long? declaredLength) => new(declaredLength, ReadAsync, IsForced: true);
    }

    private Task<ReceiveResult> ReceiveAsync(PushRequest request) => _rig.Receiver.ReceiveAsync(request, CancellationToken.None);

    private string SavedFingerprint() => Assert.IsType<SaveStatus.Found>(_rig.Cache.Current).Snapshot.Fingerprint;

    private bool HasBackupsFolderContent() => Directory.Exists(_rig.BackupsDir) && Directory.EnumerateFileSystemEntries(_rig.BackupsDir).Any();

    [Fact]
    public async Task Valid_zip_replaces_the_save_and_reports_backup_name_and_fingerprint()
    {
        var before = SaveInvariants.Capture(_rig);

        var result = await ReceiveAsync(new Body(ValidZip).Request());

        var replaced = Assert.IsType<ReceiveResult.Replaced>(result);
        Assert.Equal((ReceiverRig.ExpectedBackupName, SaveTreeBuilder.ContractFingerprint), (replaced.BackupName, replaced.Fingerprint));
        SaveInvariants.AssertSaveIsExactlyTheZip(_rig, before, ZipBuilder.Unwrap(SaveZipValidator.Validate(ValidZip)));
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, Fingerprint.OfDirectory(_rig.SaveDir));
    }

    [Fact]
    public async Task The_backup_made_by_a_successful_receive_is_the_save_as_it_was_before()
    {
        var before = Fingerprint.OfDirectory(_rig.SaveDir);

        await ReceiveAsync(new Body(ValidZip).Request());

        SaveInvariants.AssertEveryBackupIsACompleteCopyOf(_rig, before);
        Assert.True(File.Exists(Path.Combine(_rig.BackupsDir, ReceiverRig.ExpectedBackupName)));
    }

    [Fact]
    public async Task The_backup_already_exists_when_the_replacement_starts_touching_the_save()
    {
        var backupExistedAtFirstStageStep = false;
        _rig.Ops.OnBefore = call =>
        {
            if (call.Op == FileOp.CreateDirectory && IsStagePath(call.Path))
            {
                backupExistedAtFirstStageStep = File.Exists(Path.Combine(_rig.BackupsDir, ReceiverRig.ExpectedBackupName));
            }
        };

        await ReceiveAsync(new Body(ValidZip).Request());

        Assert.True(backupExistedAtFirstStageStep);
    }

    private static bool IsStagePath(string path) => path.Contains(".totksync-new-", StringComparison.Ordinal);

    [Fact]
    public async Task Successful_receive_publishes_the_new_fingerprint_to_the_status_cache_and_raises_changed()
    {
        var changes = 0;
        _rig.Cache.Changed += () => changes++;

        await ReceiveAsync(new Body(ValidZip).Request());

        Assert.Equal((SaveTreeBuilder.ContractFingerprint, 1), (SavedFingerprint(), changes));
    }

    [Fact]
    public async Task Missing_content_length_is_refused_without_reading_the_body_or_touching_anything()
    {
        var body = new Body(ValidZip);
        var before = SaveInvariants.Capture(_rig);

        var result = await ReceiveAsync(body.Request(null));

        Assert.IsType<ReceiveResult.LengthRequired>(result);
        Assert.Equal(0, body.Reads);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
    }

    [Fact]
    public async Task One_byte_over_the_limit_is_refused_without_reading_the_body()
    {
        var body = new Body(ValidZip);

        var result = await ReceiveAsync(body.Request(MaxZipBytes + 1));

        Assert.IsType<ReceiveResult.TooLarge>(result);
        Assert.Equal(0, body.Reads);
        Assert.False(Directory.Exists(_rig.BackupsDir));
    }

    [Fact]
    public async Task Exactly_the_limit_is_still_read()
    {
        var body = new Body(ValidZip);

        var result = await ReceiveAsync(body.Request(MaxZipBytes));

        Assert.IsType<ReceiveResult.Replaced>(result);
        Assert.Equal(1, body.Reads);
    }

    [Fact]
    public async Task Empty_body_is_not_a_zip()
    {
        var result = await ReceiveAsync(new Body([]).Request());

        Assert.Equal(new ReceiveResult.InvalidZip(SaveZipReasons.NotAZip), result);
    }

    [Fact]
    public async Task Refusals_before_reading_do_not_leave_the_gate_busy()
    {
        await ReceiveAsync(new Body(ValidZip).Request(null));
        await ReceiveAsync(new Body(ValidZip).Request(MaxZipBytes + 1));
        _rig.Game.Running = true;
        await ReceiveAsync(new Body(ValidZip).Request());

        Assert.False(_rig.Gate.IsBusy);
    }

    [Fact]
    public async Task Game_running_at_the_start_is_refused_before_the_body_is_read_and_without_a_backup()
    {
        _rig.Game.Running = true;
        var body = new Body(ValidZip);
        var before = SaveInvariants.Capture(_rig);

        var result = await ReceiveAsync(body.Request());

        Assert.IsType<ReceiveResult.GameRunning>(result);
        Assert.Equal(0, body.Reads);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
    }

    [Fact]
    public async Task Game_started_while_the_body_was_uploading_is_refused_before_any_backup()
    {
        var zip = ValidZip;
        var before = SaveInvariants.Capture(_rig);
        var request = new PushRequest(zip.Length, _ =>
        {
            _rig.Game.Running = true;
            return Task.FromResult(zip);
        }, IsForced: true);

        var result = await ReceiveAsync(request);

        Assert.IsType<ReceiveResult.GameRunning>(result);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
        Assert.Empty(_rig.Ops.Calls);
    }

    [Fact]
    public async Task Game_started_while_the_backup_was_written_stops_before_the_swap_and_leaves_a_complete_backup_only()
    {
        var before = SaveInvariants.Capture(_rig);
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.WriteFile && call.Path.EndsWith(".part"))
            {
                _rig.Game.Running = true;
            }
        };

        var result = await ReceiveAsync(new Body(ValidZip).Request());

        Assert.IsType<ReceiveResult.GameRunning>(result);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        SaveInvariants.AssertEveryBackupIsACompleteCopyOf(_rig, before.SaveFingerprint);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.Temp.Path));
        Assert.Equal(0, _rig.Ops.CountOf(FileOp.MoveDirectory));
    }

    [Fact]
    public async Task Game_started_while_the_stage_was_written_stops_before_the_swap()
    {
        var before = SaveInvariants.Capture(_rig);
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.WriteFile && IsStagePath(call.Path))
            {
                _rig.Game.Running = true;
            }
        };

        var result = await ReceiveAsync(new Body(ValidZip).Request());

        Assert.IsType<ReceiveResult.GameRunning>(result);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.Temp.Path));
    }

    [Fact]
    public async Task Game_refusal_after_the_body_was_read_does_not_leave_the_gate_busy()
    {
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.WriteFile)
            {
                _rig.Game.Running = true;
            }
        };

        await ReceiveAsync(new Body(ValidZip).Request());

        Assert.False(_rig.Gate.IsBusy);
    }

    [Fact]
    public async Task Game_folder_without_a_save_gives_no_save_and_creates_no_save_folder_or_backup()
    {
        using var rig = await ReceiverRig.CreateAsync(withSave: false);
        var gameBefore = TreeSnapshot.Capture(rig.GameDir);

        var result = await rig.Receiver.ReceiveAsync(new Body(ValidZip).Request(), CancellationToken.None);

        Assert.IsType<ReceiveResult.NoSave>(result);
        Assert.Equal(gameBefore, TreeSnapshot.Capture(rig.GameDir));
        Assert.False(Directory.Exists(rig.BackupsDir));
        Assert.False(Directory.Exists(rig.SaveDir));
    }

    [Fact]
    public async Task Save_folder_override_that_is_not_a_save_gives_no_save_and_is_left_alone()
    {
        using var rig = await ReceiverRig.CreateAsync(withSave: false);
        var notASave = rig.Temp.Combine("documents");
        SaveTreeBuilder.WriteFile(notASave, "my-thesis.docx", "precious");
        await rig.Settings.SetSaveDirAsync(notASave);
        var before = TreeSnapshot.Capture(notASave);

        var result = await rig.Receiver.ReceiveAsync(new Body(ValidZip).Request(), CancellationToken.None);

        Assert.IsType<ReceiveResult.NoSave>(result);
        Assert.Equal(before, TreeSnapshot.Capture(notASave));
        Assert.False(Directory.Exists(rig.BackupsDir));
    }

    [Fact]
    public async Task Save_folder_override_that_does_not_exist_gives_no_save_and_is_not_created()
    {
        using var rig = await ReceiverRig.CreateAsync(withSave: false);
        var missing = rig.Temp.Combine("not-there");
        await rig.Settings.SetSaveDirAsync(missing);

        var result = await rig.Receiver.ReceiveAsync(new Body(ValidZip).Request(), CancellationToken.None);

        Assert.IsType<ReceiveResult.NoSave>(result);
        Assert.False(Directory.Exists(missing));
    }

    [Fact]
    public async Task Several_save_folders_are_refused_with_the_choose_one_message_and_nothing_changes()
    {
        SaveTreeBuilder.BuildMinimalSave(Path.Combine(_rig.ProfileDir, "B"));
        var before = TreeSnapshot.Capture(_rig.GameDir);

        var result = await ReceiveAsync(new Body(ValidZip).Request());

        Assert.Equal(new ReceiveResult.Failed(SaveReceiver.SeveralSaves), result);
        Assert.Equal(before, TreeSnapshot.Capture(_rig.GameDir));
    }

    public static TheoryData<string> InvalidVectors()
    {
        var data = new TheoryData<string>();
        foreach (var file in Directory.GetFiles(Path.Combine(AppContext.BaseDirectory, "contracts", "save-zip"), "invalid-*.zip").Select(path => Path.GetFileName(path)))
        {
            data.Add(file);
        }
        return data;
    }

    [Theory]
    [MemberData(nameof(InvalidVectors))]
    public async Task Every_invalid_contract_vector_is_refused_with_its_reason_and_no_backup_and_no_change(string file)
    {
        var before = SaveInvariants.Capture(_rig);
        var expected = SaveZipValidator.Validate(ZipBuilder.ContractZip(file));

        var result = await ReceiveAsync(new Body(ZipBuilder.ContractZip(file)).Request());

        var invalid = Assert.IsType<ReceiveResult.InvalidZip>(result);
        Assert.Equal(Assert.IsType<ZipValidation.Invalid>(expected).Reason, invalid.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(HasBackupsFolderContent());
        Assert.Empty(TreeSnapshot.Leftovers(_rig.Temp.Path));
        Assert.Empty(_rig.Ops.Calls);
    }

    [Fact]
    public async Task A_second_receive_while_one_is_in_progress_is_refused_as_busy_without_reading_its_body()
    {
        var zip = ValidZip;
        var entered = new TaskCompletionSource();
        var release = new TaskCompletionSource();
        var first = ReceiveAsync(new PushRequest(zip.Length, async _ =>
        {
            entered.SetResult();
            await release.Task;
            return zip;
        }, IsForced: true));
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(10));
        var secondBody = new Body(zip);

        var second = await ReceiveAsync(secondBody.Request());
        release.SetResult();
        var firstResult = await first.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.IsType<ReceiveResult.Busy>(second);
        Assert.Equal(0, secondBody.Reads);
        Assert.IsType<ReceiveResult.Replaced>(firstResult);
    }

    [Fact]
    public async Task Gate_is_free_again_after_a_receive_so_the_next_one_is_served()
    {
        await ReceiveAsync(new Body(ValidZip).Request());

        var next = await ReceiveAsync(new Body(ValidZip).Request());

        Assert.IsType<ReceiveResult.Unchanged>(next);
        Assert.False(_rig.Gate.IsBusy);
    }

    [Fact]
    public async Task Gate_is_busy_exactly_while_the_receive_runs_and_reports_both_changes()
    {
        var changes = new List<bool>();
        _rig.Gate.BusyChanged += changes.Add;

        await ReceiveAsync(new Body(ValidZip).Request());

        Assert.Equal([true, false], changes);
    }

    [Fact]
    public async Task While_the_new_save_is_being_written_the_status_still_shows_the_old_save_and_the_gate_is_busy()
    {
        var oldFingerprint = SavedFingerprint();
        var reached = new ManualResetEventSlim();
        var release = new ManualResetEventSlim();
        _rig.Ops.OnBefore = call =>
        {
            if (call.Op == FileOp.WriteFile && IsStagePath(call.Path) && !reached.IsSet)
            {
                reached.Set();
                release.Wait(TimeSpan.FromSeconds(20));
            }
        };
        var receiving = ReceiveAsync(new Body(ValidZip).Request());
        Assert.True(reached.Wait(TimeSpan.FromSeconds(10)));

        var statusWhileWriting = _rig.Query.Get().Save!.Fingerprint;
        var isBusy = _rig.Gate.IsBusy;
        release.Set();
        await receiving.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Equal((oldFingerprint, true), (statusWhileWriting, isBusy));
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, _rig.Query.Get().Save!.Fingerprint);
    }

    [Fact]
    public async Task Unreadable_save_makes_the_backup_fail_and_the_save_stays_exactly_as_it_was()
    {
        using var locked = new FileStream(Path.Combine(_rig.SaveDir, "option.sav"), FileMode.Open, FileAccess.Read, FileShare.None);

        var result = await ReceiveAsync(new Body(ValidZip).Request());

        var failed = Assert.IsType<ReceiveResult.Failed>(result);
        Assert.StartsWith("backup failed, PC save not changed: ", failed.Reason);
        Assert.Equal(0, _rig.Ops.CountOf(FileOp.MoveDirectory));
        Assert.False(HasBackupsFolderContent());
    }

    [Theory]
    [InlineData(FileOp.CreateDirectory)]
    [InlineData(FileOp.WriteFile)]
    [InlineData(FileOp.MoveFile)]
    public async Task Backup_failure_is_write_failed_with_the_save_untouched_and_the_replacement_never_started(FileOp failingBackupStep)
    {
        var before = SaveInvariants.Capture(_rig);
        _rig.Ops.FailOnNth(failingBackupStep, 1);

        var result = await ReceiveAsync(new Body(ValidZip).Request());

        var failed = Assert.IsType<ReceiveResult.Failed>(result);
        Assert.StartsWith("backup failed, PC save not changed: ", failed.Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Equal(0, _rig.Ops.CountOf(FileOp.MoveDirectory));
        Assert.Empty(TreeSnapshot.Leftovers(_rig.Temp.Path));
        Assert.False(HasBackupsFolderContent());
    }

    [Fact]
    public async Task Access_denied_while_backing_up_is_also_reported_as_a_backup_failure()
    {
        _rig.Ops.FailOnNth(FileOp.WriteFile, 1);
        _rig.Ops.CreateFailure = message => new UnauthorizedAccessException(message);

        var result = await ReceiveAsync(new Body(ValidZip).Request());

        Assert.StartsWith("backup failed, PC save not changed: ", Assert.IsType<ReceiveResult.Failed>(result).Reason);
    }

    [Fact]
    public async Task Replacement_failure_is_reported_keeps_the_save_and_does_not_publish_a_new_fingerprint()
    {
        var before = SaveInvariants.Capture(_rig);
        var published = _rig.Cache.Current;
        _rig.Ops.FailOnNth(FileOp.MoveDirectory, 2);

        var result = await ReceiveAsync(new Body(ValidZip).Request());

        Assert.StartsWith("couldn't install the new save", Assert.IsType<ReceiveResult.Failed>(result).Reason);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Same(published, _rig.Cache.Current);
        Assert.False(_rig.Gate.IsBusy);
    }

    [Fact]
    public async Task Upload_that_breaks_off_is_reported_as_interrupted_and_nothing_is_written()
    {
        var before = SaveInvariants.Capture(_rig);
        var request = new PushRequest(ValidZip.Length, _ => throw new IOException("connection reset"));

        var result = await ReceiveAsync(request);

        Assert.Equal(new ReceiveResult.Failed(SaveReceiver.UploadInterrupted), result);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
        Assert.False(_rig.Gate.IsBusy);
    }

    [Fact]
    public async Task Upload_that_times_out_or_is_cancelled_is_reported_as_interrupted()
    {
        var request = new PushRequest(ValidZip.Length, _ => throw new OperationCanceledException());

        var result = await ReceiveAsync(request);

        Assert.Equal(new ReceiveResult.Failed(SaveReceiver.UploadInterrupted), result);
    }

    [Fact]
    public async Task Unexpected_error_while_reading_propagates_but_still_releases_the_gate()
    {
        var request = new PushRequest(ValidZip.Length, _ => throw new InvalidOperationException("bug"));

        await Assert.ThrowsAsync<InvalidOperationException>(() => ReceiveAsync(request));

        Assert.False(_rig.Gate.IsBusy);
    }

    private sealed class GameThatThrowsOnCall(int throwingCall) : TotkSync.Core.Game.IGameProcess
    {
        private int _calls;

        public bool IsRunning() => ++_calls == throwingCall ? throw new InvalidOperationException("boom") : false;
    }

    [Fact]
    public async Task Unexpected_exception_while_replacing_is_write_failed_naming_the_backup_and_leaves_no_stage()
    {
        var before = SaveInvariants.Capture(_rig);
        var game = new GameThatThrowsOnCall(throwingCall: 3);
        var receiver = new SaveReceiver(_rig.Settings, game, _rig.Gate, _rig.Cache, _rig.Backups, new TotkSync.Core.Replace.MirrorReplacer(_rig.Ops, game, TotkSync.Core.Replace.SwapRetry.None), _rig.SyncState, _rig.Time);

        var result = await receiver.ReceiveAsync(new Body(ValidZip).Request(), CancellationToken.None);

        var failed = Assert.IsType<ReceiveResult.Failed>(result);
        Assert.Contains(ReceiverRig.ExpectedBackupName, failed.Reason, StringComparison.Ordinal);
        Assert.Contains("boom", failed.Reason, StringComparison.Ordinal);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public async Task Empty_game_folder_setting_is_no_save()
    {
        var settings = new TotkSync.Core.Settings.SettingsSession(
            new TotkSync.Core.Settings.SettingsStore(_rig.Temp.Combine("no-game-settings.json")),
            new TotkSync.Core.Settings.LauncherSettings(GameDir: null));
        var receiver = new SaveReceiver(settings, _rig.Game, _rig.Gate, _rig.Cache, _rig.Backups, _rig.Replacer, _rig.SyncState, _rig.Time);
        var body = new Body(ValidZip);

        var result = await receiver.ReceiveAsync(body.Request(), CancellationToken.None);

        Assert.IsType<ReceiveResult.NoSave>(result);
        Assert.Equal(0, body.Reads);
        Assert.False(HasBackupsFolderContent());
    }
}
