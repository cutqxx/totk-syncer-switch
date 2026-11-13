using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Zip;

namespace TotkSync.Core.Backups;

public sealed class BackupStore(string dir, IFileOps ops, TimeProvider time)
{
    public BackupFile CreateBeforeSwitch(string saveDir) => Create(saveDir, BackupName.FirstFreeBeforeSwitch);

    public BackupFile CreateRecovered(string saveDir) => Create(saveDir, BackupName.FirstFreeRecovered);

    public IReadOnlyList<BackupEntry> List()
    {
        return ListBackupNames()
            .Select(name => (Name: name, Info: BackupName.TryParse(name)))
            .Where(item => item.Info is not null)
            .Select(item => new BackupEntry(item.Name, item.Info!.LocalTime, item.Info.Kind, new FileInfo(Path.Combine(dir, item.Name)).Length))
            .OrderByDescending(entry => entry.Name, StringComparer.Ordinal)
            .ToList();
    }

    public void PruneOldBackups(int keep, string justCreatedName)
    {
        try
        {
            ListObsoleteBackups(keep, justCreatedName).ForEach(name => DeleteQuietly(Path.Combine(dir, name)));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            // Rotation is housekeeping: the save is already replaced and must still be reported as replaced.
        }
    }

    public string? FindBackupOf(string fingerprint)
    {
        if (!Directory.Exists(dir))
        {
            return null;
        }
        return Directory.EnumerateFiles(dir)
            .Select(Path.GetFileName)
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .FirstOrDefault(name => BackupName.IsBackup(name) && IsBackupOf(Path.Combine(dir, name), fingerprint));
    }

    public void DeleteOrphanedParts()
    {
        try
        {
            ListOrphanedParts().ForEach(DeleteQuietly);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            // Orphaned .part files are harmless; the launcher must still start.
        }
    }

    private List<string> ListObsoleteBackups(int keep, string justCreatedName)
    {
        return ListBackupNames()
            .Where(name => name != justCreatedName)
            .OrderByDescending(name => name, StringComparer.Ordinal)
            .Skip(Math.Max(keep - 1, 0))
            .ToList();
    }

    private List<string> ListBackupNames()
    {
        return Directory.Exists(dir)
            ? Directory.EnumerateFiles(dir).Select(Path.GetFileName).OfType<string>().Where(BackupName.IsBackup).ToList()
            : [];
    }

    private List<string> ListOrphanedParts()
    {
        return Directory.Exists(dir)
            ? Directory.EnumerateFiles(dir).Where(path => BackupName.IsPartial(Path.GetFileName(path))).ToList()
            : [];
    }

    private BackupFile Create(string saveDir, Func<DateTimeOffset, Func<string, bool>, string> chooseName)
    {
        var packed = SaveZipPacker.Pack(saveDir);
        var name = chooseName(time.GetLocalNow(), candidate => File.Exists(Path.Combine(dir, candidate)));
        var finalPath = Path.Combine(dir, name);
        var partPath = finalPath + BackupName.PartSuffix;
        ops.CreateDirectory(dir);
        try
        {
            ops.WriteFile(partPath, packed);
            VerifyMatchesSave(partPath, saveDir);
            ops.MoveFile(partPath, finalPath, overwrite: false);
        }
        catch
        {
            DeleteQuietly(partPath);
            throw;
        }
        return new BackupFile(name, finalPath);
    }

    private static void VerifyMatchesSave(string backupPath, string saveDir)
    {
        if (!IsBackupOf(backupPath, Fingerprint.OfDirectory(saveDir)))
        {
            throw new IOException("the backup doesn't match the current save");
        }
    }

    private static bool IsBackupOf(string backupPath, string fingerprint)
    {
        try
        {
            var validation = SaveZipValidator.Validate(File.ReadAllBytes(backupPath));
            return validation is ZipValidation.Valid valid && Fingerprint.OfZip(valid.Zip) == fingerprint;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private void DeleteQuietly(string path)
    {
        try
        {
            ops.DeleteFile(path);
        }
        catch (Exception)
        {
            // The .part leftover is harmless; the original failure is what the caller must see.
        }
    }
}
