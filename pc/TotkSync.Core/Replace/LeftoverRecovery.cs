using TotkSync.Core.Backups;
using TotkSync.Core.Saves;

namespace TotkSync.Core.Replace;

public sealed class LeftoverRecovery(
    IFileOps ops,
    BackupStore backups,
    SwapRetry? retry = null,
    Func<string, IEnumerable<string>>? listDirectories = null)
{
    private readonly SwapRetry _retry = retry ?? SwapRetry.Default;
    private readonly Func<string, IEnumerable<string>> _listDirectories = listDirectories ?? Directory.EnumerateDirectories;

    public IReadOnlyList<string> Recover(string parentDir)
    {
        var notes = new List<string>();
        foreach (var leftover in TryListLeftovers(parentDir, notes).OrderBy(StagingNames.IsBad))
        {
            notes.AddRange(TryRecoverOne(leftover));
        }
        notes.AddRange(RemoveOrphanMarkers(parentDir));
        return notes;
    }

    private List<string> RemoveOrphanMarkers(string parentDir)
    {
        try
        {
            return Directory.Exists(parentDir)
                ? Directory.EnumerateFiles(parentDir).Where(IsOrphanMarker).SelectMany(RemoveMarkerWithNote).ToList()
                : [];
        }
        catch (Exception failure)
        {
            return [$"Couldn't look for stale markers of an interrupted save replace in {parentDir}: {failure.Message}"];
        }
    }

    private static bool IsOrphanMarker(string file)
    {
        return StagingNames.IsUnverifiedMarker(file) && !Directory.Exists(StagingNames.OldOfMarker(file));
    }

    private IEnumerable<string> RemoveMarkerWithNote(string marker)
    {
        var failure = _retry.Run(() => ops.DeleteFile(marker));
        return failure is null ? [] : [$"Couldn't delete the stale marker {marker}: {failure.Message}"];
    }

    private void RemoveMarker(string old)
    {
        _ = _retry.Run(() => ops.DeleteFile(StagingNames.UnverifiedMarkerFor(old)));
    }

    private List<string> TryListLeftovers(string parentDir, List<string> notes)
    {
        try
        {
            return Directory.Exists(parentDir) ? _listDirectories(parentDir).Where(StagingNames.IsLeftover).ToList() : [];
        }
        catch (Exception failure)
        {
            notes.Add($"Couldn't look for leftovers of an interrupted save replace in {parentDir}: {failure.Message}");
            return [];
        }
    }

    private IEnumerable<string> TryRecoverOne(string leftover)
    {
        try
        {
            return RecoverOne(leftover);
        }
        catch (Exception failure)
        {
            return [$"Couldn't recover {leftover}, it was left as is: {failure.Message}"];
        }
    }

    private IEnumerable<string> RecoverOne(string leftover)
    {
        if (StagingNames.IsBad(leftover))
        {
            return [DeleteIfBackedUp(leftover, "unverified save")];
        }
        if (!StagingNames.IsOld(leftover))
        {
            return Delete(leftover, "unfinished new save");
        }
        var original = StagingNames.OriginalOf(leftover);
        return Directory.Exists(original) ? RecoverOldBesideOriginal(leftover, original) : MoveBack(leftover, original);
    }

    private IEnumerable<string> RecoverOldBesideOriginal(string old, string original)
    {
        var isOriginalUntrusted = File.Exists(StagingNames.UnverifiedMarkerFor(old)) || !SaveLayout.IsSaveDirectory(original);
        return isOriginalUntrusted && SaveLayout.IsSaveDirectory(old)
            ? ReplaceUntrustedWith(old, original)
            : DeleteOldIfBackedUp(old);
    }

    private IEnumerable<string> DeleteOldIfBackedUp(string old)
    {
        var note = DeleteIfBackedUp(old, "previous save");
        if (!Directory.Exists(old))
        {
            RemoveMarker(old);
        }
        return [note];
    }

    private IEnumerable<string> ReplaceUntrustedWith(string old, string original)
    {
        var bad = StagingNames.BadFor(original);
        var asideFailure = _retry.Run(() => ops.MoveDirectory(original, bad));
        if (asideFailure is not null && TryBackUpAndDelete(original) is { } keptReason)
        {
            return [$"Kept {old} and the unverified save at {original}: {keptReason}"];
        }
        var restoreFailure = _retry.Run(() => ops.MoveDirectory(old, original));
        if (restoreFailure is not null)
        {
            return [$"Couldn't put the previous save from {old} back: {restoreFailure.Message}"];
        }
        RemoveMarker(old);
        var restored = $"The save at {original} was not verified; restored the previous save from {old}.";
        return asideFailure is null ? [restored, DeleteIfBackedUp(bad, "unverified save")] : [restored];
    }

    private string? TryBackUpAndDelete(string folder)
    {
        var failure = EnsureBackedUp(folder, out _) ?? _retry.Run(() => ops.DeleteDirectory(folder));
        return failure?.Message;
    }

    private IEnumerable<string> MoveBack(string old, string original)
    {
        var failure = _retry.Run(() => ops.MoveDirectory(old, original));
        if (failure is null)
        {
            RemoveMarker(old);
        }
        return [failure is null
            ? $"Restored the previous save from {old}."
            : $"Couldn't restore the previous save from {old}: {failure.Message}"];
    }

    private string DeleteIfBackedUp(string folder, string what)
    {
        var failure = EnsureBackedUp(folder, out var backupName);
        if (failure is not null)
        {
            return $"Kept the {what} at {folder}: no verified backup of it ({failure.Message})";
        }
        var deleteFailure = _retry.Run(() => ops.DeleteDirectory(folder));
        return deleteFailure is null
            ? $"Deleted the {what} at {folder}; {DescribeCover(backupName)}."
            : $"Couldn't delete the {what} left at {folder}: {deleteFailure.Message}";
    }

    private static string DescribeCover(string? backupName)
    {
        return backupName is null ? "it held no save files" : $"its content is in the backup {backupName}";
    }

    private Exception? EnsureBackedUp(string folder, out string? backupName)
    {
        backupName = null;
        try
        {
            if (SaveFile.Enumerate(folder).Count == 0)
            {
                return null;
            }
            backupName = backups.FindBackupOf(Fingerprint.OfDirectory(folder)) ?? backups.CreateRecovered(folder).Name;
            return null;
        }
        catch (Exception failure)
        {
            return failure;
        }
    }

    private IEnumerable<string> Delete(string folder, string what)
    {
        var failure = _retry.Run(() => ops.DeleteDirectory(folder));
        return failure is null ? [] : [$"Couldn't delete the {what} left at {folder}: {failure.Message}"];
    }
}
