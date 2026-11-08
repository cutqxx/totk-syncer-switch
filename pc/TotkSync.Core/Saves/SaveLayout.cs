namespace TotkSync.Core.Saves;

public static class SaveLayout
{
    public const string OptionFile = "option.sav";
    public const string ProgressFile = "progress.sav";
    public const int SlotCount = 6;
    public const ulong TitleId = 0x0100F2C0115B6000;

    public static string SlotDirectory(int slot) => $"slot_{slot:00}";

    public static bool IsSaveDirectory(string dir)
    {
        return File.Exists(Path.Combine(dir, OptionFile)) && HasAnySlotProgress(dir);
    }

    private static bool HasAnySlotProgress(string dir)
    {
        return Enumerable.Range(0, SlotCount)
            .Any(slot => File.Exists(Path.Combine(dir, SlotDirectory(slot), ProgressFile)));
    }
}
