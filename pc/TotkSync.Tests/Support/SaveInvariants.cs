using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Zip;

namespace TotkSync.Tests.Support;

public sealed record SaveBefore(
    SortedDictionary<string, string> SaveTree,
    string SaveFingerprint,
    SortedDictionary<string, string> Neighbours);

/// <summary>The "never lose the save" checks shared by every failure test.</summary>
public static class SaveInvariants
{
    public static SaveBefore Capture(ReceiverRig rig)
    {
        return new SaveBefore(rig.SaveTree(), Fingerprint.OfDirectory(rig.SaveDir), rig.Neighbours());
    }

    public static void AssertSaveIsUntouched(ReceiverRig rig, SaveBefore before)
    {
        Assert.Equal(before.SaveTree, rig.SaveTree());
        Assert.Equal(before.Neighbours, rig.Neighbours());
    }

    public static void AssertSaveIsExactlyTheZip(ReceiverRig rig, SaveBefore before, ValidatedSaveZip zip)
    {
        Assert.Equal(Fingerprint.OfZip(zip), Fingerprint.OfDirectory(rig.SaveDir));
        Assert.All(zip.Directories, directory => Assert.True(Directory.Exists(Path.Combine(rig.SaveDir, directory)), $"missing directory {directory}"));
        var current = rig.SaveTree();
        var oldJunk = before.SaveTree.Where(pair => pair.Value != "<dir>" && JunkFilter.IsJunk(pair.Key));
        Assert.All(oldJunk, junk => Assert.Equal(junk.Value, current.GetValueOrDefault(junk.Key)));
        Assert.Equal(before.Neighbours, rig.Neighbours());
    }

    public static void AssertSaveIsOldOrExactlyTheZip(ReceiverRig rig, SaveBefore before, ValidatedSaveZip zip)
    {
        if (rig.SaveTree().SequenceEqual(before.SaveTree))
        {
            Assert.Equal(before.Neighbours, rig.Neighbours());
            return;
        }
        AssertSaveIsExactlyTheZip(rig, before, zip);
    }

    /// <summary>Real recovery (no injected faults) must clean up and must not change a save that is already good.</summary>
    public static void AssertRecoveryCleansUpWithoutChangingTheSave(ReceiverRig rig)
    {
        var saveBeforeRecovery = rig.SaveTree();
        rig.RealRecovery().Recover(rig.ProfileDir);

        Assert.Empty(TreeSnapshot.Leftovers(rig.ProfileDir));
        Assert.Equal(saveBeforeRecovery, rig.SaveTree());
    }

    public static void AssertEveryBackupIsACompleteCopyOf(ReceiverRig rig, string expectedFingerprint)
    {
        if (!Directory.Exists(rig.BackupsDir))
        {
            return;
        }
        foreach (var backup in Directory.GetFiles(rig.BackupsDir).Where(path => !path.EndsWith(".part")))
        {
            var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(File.ReadAllBytes(backup)));
            Assert.Equal(expectedFingerprint, Fingerprint.OfZip(zip));
        }
    }
}
