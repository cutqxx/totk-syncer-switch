using TotkSync.Core.Saves;

namespace TotkSync.Core.Zip;

internal static class SaveZipStructure
{
    private const int UnixTypeMask = 0xF000;
    private const int UnixSymlinkType = 0xA000;
    private const int WindowsReparsePointAttribute = 0x400;

    public static string? FindError(IReadOnlyList<ZipItem> items)
    {
        return CountError(items)
            ?? DeclaredSizeError(items)
            ?? CompressionRatioError(items)
            ?? EntryError(items)
            ?? DuplicateError(items)
            ?? ConflictError(items)
            ?? LayoutError(items);
    }

    private static string? CountError(IReadOnlyList<ZipItem> items)
    {
        return items.Count > SaveZipLimits.MaxEntries ? SaveZipReasons.TooManyEntries : null;
    }

    private static string? DeclaredSizeError(IReadOnlyList<ZipItem> items)
    {
        return items.Sum(item => item.Entry.Length) > SaveZipLimits.MaxUnpackedBytes ? SaveZipReasons.TooLarge : null;
    }

    private static string? CompressionRatioError(IReadOnlyList<ZipItem> items)
    {
        var unpacked = items.Sum(item => item.Entry.Length);
        var packed = Math.Max(1, items.Sum(item => item.Entry.CompressedLength));
        var isSuspicious = unpacked >= SaveZipLimits.RatioCheckFromBytes && unpacked / packed > SaveZipLimits.MaxCompressionRatio;
        return isSuspicious ? SaveZipReasons.SuspiciousCompression : null;
    }

    private static string? EntryError(IReadOnlyList<ZipItem> items)
    {
        foreach (var item in items)
        {
            if (!ZipPathRules.IsSafe(item.Path))
            {
                return SaveZipReasons.UnsafePath(item.Path);
            }
            if (IsSymlink(item))
            {
                return SaveZipReasons.SymlinkEntry(item.Path);
            }
        }
        return null;
    }

    private static bool IsSymlink(ZipItem item)
    {
        var attributes = item.Entry.ExternalAttributes;
        return ((attributes >> 16) & UnixTypeMask) == UnixSymlinkType
            || (attributes & WindowsReparsePointAttribute) != 0;
    }

    private static string? DuplicateError(IReadOnlyList<ZipItem> items)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var duplicate = items.FirstOrDefault(item => !seen.Add(item.Path));
        return duplicate is null ? null : SaveZipReasons.DuplicatePath(duplicate.Path);
    }

    private static string? ConflictError(IReadOnlyList<ZipItem> items)
    {
        var files = items.Where(item => !item.IsDirectory).Select(item => item.Path).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var conflict = items.FirstOrDefault(item => Ancestors(item.Path).Any(files.Contains));
        return conflict is null ? null : SaveZipReasons.ConflictingPaths(conflict.Path);
    }

    private static IEnumerable<string> Ancestors(string path)
    {
        for (var slash = path.IndexOf('/'); slash >= 0; slash = path.IndexOf('/', slash + 1))
        {
            yield return path[..slash];
        }
    }

    private static string? LayoutError(IReadOnlyList<ZipItem> items)
    {
        var files = items.Where(item => !item.IsDirectory).Select(item => item.Path).ToHashSet(StringComparer.Ordinal);
        if (!files.Contains(SaveLayout.OptionFile))
        {
            return SaveZipReasons.MissingOption;
        }
        return HasSlotProgress(files) ? null : SaveZipReasons.MissingProgress;
    }

    private static bool HasSlotProgress(HashSet<string> files)
    {
        return Enumerable.Range(0, SaveLayout.SlotCount)
            .Any(slot => files.Contains($"{SaveLayout.SlotDirectory(slot)}/{SaveLayout.ProgressFile}"));
    }
}
