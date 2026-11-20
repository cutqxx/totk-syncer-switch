namespace TotkSync.Tests.Support;

public static class SaveTreeBuilder
{
    public static string ContractTreeDir =>
        Path.Combine(AppContext.BaseDirectory, "contracts", "save-tree");

    public static string ContractFingerprint =>
        File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "contracts", "save-tree.fingerprint.txt")).Trim();

    public static string BuildMinimalSave(string saveDir)
    {
        WriteFile(saveDir, "option.sav", "option");
        WriteFile(saveDir, "slot_00/progress.sav", "progress");
        return saveDir;
    }

    public static string BuildFullSave(string saveDir)
    {
        WriteFile(saveDir, "option.sav", "option");
        foreach (var slot in Enumerable.Range(0, 6))
        {
            WriteSlot(saveDir, slot);
        }
        WriteFile(saveDir, "storage/CacheStorageKey.dat", "cache");
        Directory.CreateDirectory(Path.Combine(saveDir, "album"));
        Directory.CreateDirectory(Path.Combine(saveDir, "picturebook"));
        return saveDir;
    }

    public static string BuildGameDirWithSave(string gameDir, string profileSubDir)
    {
        WriteFile(gameDir, "totk.exe", "exe");
        return BuildMinimalSave(Path.Combine(gameDir, "profile", profileSubDir));
    }

    public static void WriteFile(string root, string relPath, string content)
    {
        var fullPath = Path.Combine(root, relPath);
        Directory.CreateDirectory(Path.GetDirectoryName(fullPath)!);
        File.WriteAllText(fullPath, content);
    }

    private static void WriteSlot(string saveDir, int slot)
    {
        foreach (var name in new[] { "caption", "direct_file_save_related", "footprint", "progress" })
        {
            WriteFile(saveDir, $"slot_{slot:00}/{name}.sav", $"slot{slot}-{name}");
        }
    }
}
