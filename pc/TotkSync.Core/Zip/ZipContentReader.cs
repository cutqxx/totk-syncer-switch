using TotkSync.Core.Saves;

namespace TotkSync.Core.Zip;

internal sealed record ZipContents(IReadOnlyList<(ZipItem Item, byte[] Content)> Files, string? BadEntryPath);

internal static class ZipContentReader
{
    public static ZipContents ReadAll(IReadOnlyList<ZipItem> items)
    {
        var files = new List<(ZipItem, byte[])>();
        foreach (var item in items.Where(item => !item.IsDirectory))
        {
            var content = TryRead(item);
            if (content is null)
            {
                return new ZipContents([], item.Path);
            }
            files.Add((item, content));
        }
        return new ZipContents(files, null);
    }

    private static byte[]? TryRead(ZipItem item)
    {
        try
        {
            var content = ReadExactlyDeclaredLength(item);
            return content is not null && Crc32.Compute(content) == item.Entry.Crc32 ? WithoutJunkBody(item, content) : null;
        }
        catch (Exception error) when (error is InvalidDataException or IOException or NotSupportedException)
        {
            return null;
        }
    }

    private static byte[]? ReadExactlyDeclaredLength(ZipItem item)
    {
        using var stream = item.Entry.Open();
        var content = new byte[item.Entry.Length];
        stream.ReadExactly(content);
        return stream.ReadByte() == -1 ? content : null;
    }

    private static byte[] WithoutJunkBody(ZipItem item, byte[] content)
    {
        var isDiscardable = JunkFilter.IsJunk(item.Path) && !JunkFilter.IsMeta(item.Path);
        return isDiscardable ? [] : content;
    }
}
