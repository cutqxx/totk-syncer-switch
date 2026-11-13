using TotkSync.Core.Replace;
using TotkSync.Core.Sync;
using TotkSync.Core.Zip;
using TotkSync.Tests.Support;
using Xunit.Abstractions;

namespace TotkSync.Tests.Core;

/// <summary>
/// Injects a failure into the N-th call of every file operation, for N = 1.. until the operation
/// is no longer reached, and checks that the PC save is always either the old one or the new one.
/// </summary>
public sealed class ReplaceFailureSweepTests(ITestOutputHelper output)
{
    private static readonly FileOp[] ReplacerOps = [FileOp.CreateDirectory, FileOp.WriteFile, FileOp.CopyFile, FileOp.MoveDirectory, FileOp.DeleteDirectory];
    private static readonly FileOp[] ReceiverOps = [.. ReplacerOps, FileOp.MoveFile];
    private static readonly FileOp[] PartialEffectOps = [FileOp.WriteFile, FileOp.CopyFile, FileOp.MoveDirectory, FileOp.MoveFile, FileOp.DeleteDirectory, FileOp.DeleteFile];

    public static TheoryData<FileOp, FaultTiming> ReplacerSingleFaults() => Faults(ReplacerOps);

    public static TheoryData<FileOp, FaultTiming> ReceiverSingleFaults() => Faults(ReceiverOps);

    public static TheoryData<FileOp> ReceiverOpsData() => new(ReceiverOps);

    private static TheoryData<FileOp, FaultTiming> Faults(FileOp[] ops)
    {
        var data = new TheoryData<FileOp, FaultTiming>();
        foreach (var op in ops)
        {
            data.Add(op, FaultTiming.BeforeOperation);
            if (PartialEffectOps.Contains(op))
            {
                data.Add(op, FaultTiming.AfterOperation);
            }
        }
        return data;
    }

    private static byte[] ZipBytes => ZipBuilder.ContractZip("valid-save-tree.zip");

    private static ValidatedSaveZip NewSave => ZipBuilder.Unwrap(SaveZipValidator.Validate(ZipBytes));

    private static PushRequest Push() => new(ZipBytes.Length, _ => Task.FromResult(ZipBytes), IsForced: true);

    [Theory]
    [MemberData(nameof(ReplacerSingleFaults))]
    public async Task Replacer_failing_once_at_every_call_of_an_operation_leaves_the_old_or_the_new_save_and_no_leftovers(FileOp op, FaultTiming timing)
    {
        var points = 0;
        for (var nth = 1; ; nth++)
        {
            using var rig = await ReceiverRig.CreateAsync();
            var before = SaveInvariants.Capture(rig);
            rig.Ops.FailOnNth(op, nth, timing);

            var result = rig.Replacer.Replace(rig.SaveDir, NewSave);

            if (!rig.Ops.HasFailed)
            {
                Assert.IsType<ReplaceResult.Replaced>(result);
                break;
            }
            points++;
            AssertOutcomeIsSafe(rig, before, result, op);
        }
        Assert.True(points > 0, $"{op} was never called");
        output.WriteLine($"Replacer {op} {timing}: {points} failure points, all safe");
    }

    [Theory]
    [MemberData(nameof(ReceiverSingleFaults))]
    public async Task Receiver_failing_once_at_every_call_of_an_operation_leaves_the_old_or_the_new_save_and_complete_backups_only(FileOp op, FaultTiming timing)
    {
        var points = 0;
        for (var nth = 1; ; nth++)
        {
            using var rig = await ReceiverRig.CreateAsync();
            var before = SaveInvariants.Capture(rig);
            rig.Ops.FailOnNth(op, nth, timing);

            var result = await rig.Receiver.ReceiveAsync(Push(), CancellationToken.None);

            if (!rig.Ops.HasFailed)
            {
                Assert.IsType<ReceiveResult.Replaced>(result);
                break;
            }
            points++;
            AssertReceiverOutcomeIsSafe(rig, before, result, op);
        }
        Assert.True(points > 0, $"{op} was never called");
        output.WriteLine($"Receiver {op} {timing}: {points} failure points, all safe");
    }

    [Theory]
    [MemberData(nameof(ReceiverOpsData))]
    public async Task Receiver_failing_at_every_call_of_an_operation_while_all_cleanup_fails_too_keeps_a_good_save_and_recovery_cleans_up(FileOp op)
    {
        var points = 0;
        for (var nth = 1; ; nth++)
        {
            using var rig = await ReceiverRig.CreateAsync();
            var before = SaveInvariants.Capture(rig);
            var isPrimaryFaultReached = false;
            rig.Ops.FailWhen = (call, number) =>
            {
                var isPrimary = call.Op == op && number == nth;
                isPrimaryFaultReached |= isPrimary;
                return isPrimary || call.Op is FileOp.DeleteDirectory or FileOp.DeleteFile;
            };

            var result = await rig.Receiver.ReceiveAsync(Push(), CancellationToken.None);

            if (!isPrimaryFaultReached)
            {
                break;
            }
            points++;
            AssertSaveIsOldOrNew(rig, before, result);
            SaveInvariants.AssertRecoveryCleansUpWithoutChangingTheSave(rig);
            SaveInvariants.AssertSaveIsOldOrExactlyTheZip(rig, before, NewSave);
        }
        Assert.True(points > 0, $"{op} was never called");
        output.WriteLine($"Receiver {op} + failing cleanup: {points} failure points, all safe");
    }

    [Theory]
    [InlineData(FileOp.CreateDirectory)]
    [InlineData(FileOp.WriteFile)]
    [InlineData(FileOp.CopyFile)]
    [InlineData(FileOp.DeleteDirectory)]
    public async Task Replacer_where_one_operation_fails_from_the_Nth_call_on_keeps_the_old_or_the_new_save(FileOp op)
    {
        var points = 0;
        for (var nth = 1; ; nth++)
        {
            using var rig = await ReceiverRig.CreateAsync();
            var before = SaveInvariants.Capture(rig);
            rig.Ops.FailFromNth(op, nth);

            var result = rig.Replacer.Replace(rig.SaveDir, NewSave);

            if (!rig.Ops.HasFailed)
            {
                break;
            }
            points++;
            AssertOutcomeIsSafe(rig, before, result, op);
        }
        Assert.True(points > 0, $"{op} was never called");
        output.WriteLine($"Replacer {op} persistent: {points} failure points, all safe");
    }

    [Fact]
    public async Task Replacer_whose_first_directory_move_always_fails_leaves_the_save_untouched()
    {
        using var rig = await ReceiverRig.CreateAsync();
        var before = SaveInvariants.Capture(rig);
        rig.Ops.FailFromNth(FileOp.MoveDirectory, 1);

        var result = rig.Replacer.Replace(rig.SaveDir, NewSave);

        AssertOutcomeIsSafe(rig, before, result, FileOp.MoveDirectory);
        Assert.IsType<ReplaceResult.Failed>(result);
    }

    private static void AssertOutcomeIsSafe(ReceiverRig rig, SaveBefore before, ReplaceResult result, FileOp failedOp)
    {
        var mayLeaveLeftovers = failedOp == FileOp.DeleteDirectory;
        switch (result)
        {
            case ReplaceResult.Replaced:
                SaveInvariants.AssertSaveIsExactlyTheZip(rig, before, NewSave);
                break;
            case ReplaceResult.Failed or ReplaceResult.GameRunning:
                SaveInvariants.AssertSaveIsUntouched(rig, before);
                break;
        }
        if (mayLeaveLeftovers)
        {
            SaveInvariants.AssertRecoveryCleansUpWithoutChangingTheSave(rig);
        }
        Assert.Empty(TreeSnapshot.Leftovers(rig.GameDir));
    }

    private static void AssertReceiverOutcomeIsSafe(ReceiverRig rig, SaveBefore before, ReceiveResult result, FileOp failedOp)
    {
        AssertSaveIsOldOrNew(rig, before, result);
        if (failedOp == FileOp.DeleteDirectory)
        {
            SaveInvariants.AssertRecoveryCleansUpWithoutChangingTheSave(rig);
        }
        Assert.Empty(TreeSnapshot.Leftovers(rig.Temp.Path));
        SaveInvariants.AssertEveryBackupIsACompleteCopyOf(rig, before.SaveFingerprint);
    }

    private static void AssertSaveIsOldOrNew(ReceiverRig rig, SaveBefore before, ReceiveResult result)
    {
        if (result is ReceiveResult.Replaced replaced)
        {
            SaveInvariants.AssertSaveIsExactlyTheZip(rig, before, NewSave);
            Assert.Equal(ReceiverRig.ExpectedBackupName, replaced.BackupName);
            Assert.True(File.Exists(Path.Combine(rig.BackupsDir, replaced.BackupName)), "the reported backup must exist");
        }
        else
        {
            Assert.IsType<ReceiveResult.Failed>(result);
            SaveInvariants.AssertSaveIsUntouched(rig, before);
        }
    }
}
