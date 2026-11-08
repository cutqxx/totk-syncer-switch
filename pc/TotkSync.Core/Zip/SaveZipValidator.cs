using System.IO.Compression;
using System.Runtime.InteropServices;
using TotkSync.Core.Saves;

namespace TotkSync.Core.Zip;

public static class SaveZipValidator
{
    public static ZipValidation Validate(ReadOnlyMemory<byte> zip)
    {
        try
        {
            using var archive = new ZipArchive(AsStream(zip), ZipArchiveMode.Read);
            return ValidateItems(archive.Entries.Select(ZipItem.From).ToList());
        }
        catch (Exception error) when (error is InvalidDataException or IOException or NotSupportedException)
        {
            return new ZipValidation.Invalid(SaveZipReasons.NotAZip);
        }
    }

    private static ZipValidation ValidateItems(IReadOnlyList<ZipItem> items)
    {
        var structureError = SaveZipStructure.FindError(items);
        if (structureError is not null)
        {
            return new ZipValidation.Invalid(structureError);
        }
        var contents = ZipContentReader.ReadAll(items);
        if (contents.BadEntryPath is { } badPath)
        {
            return new ZipValidation.Invalid(SaveZipReasons.BadCrc(badPath));
        }
        return HasForeignMeta(contents)
            ? new ZipValidation.Invalid(SaveZipReasons.AnotherGame)
            : new ZipValidation.Valid(ToSaveContent(items, contents));
    }

    private static bool HasForeignMeta(ZipContents contents)
    {
        return contents.Files
            .Where(file => JunkFilter.IsMeta(file.Item.Path))
            .Any(file => !SaveMeta.IsForTotk(file.Content));
    }

    private static ValidatedSaveZip ToSaveContent(IReadOnlyList<ZipItem> items, ZipContents contents)
    {
        var files = contents.Files
            .Where(file => !JunkFilter.IsJunk(file.Item.Path))
            .Select(file => new SaveZipFile(file.Item.Path, file.Content))
            .ToList();
        var directories = items
            .Where(item => item.IsDirectory && !JunkFilter.IsJunk(item.Path))
            .Select(item => item.Path)
            .ToList();
        return new ValidatedSaveZip(files, directories);
    }

    private static MemoryStream AsStream(ReadOnlyMemory<byte> zip)
    {
        return MemoryMarshal.TryGetArray(zip, out var segment) && segment.Offset == 0 && segment.Count == segment.Array!.Length
            ? new MemoryStream(segment.Array, writable: false)
            : new MemoryStream(zip.ToArray(), writable: false);
    }
}
