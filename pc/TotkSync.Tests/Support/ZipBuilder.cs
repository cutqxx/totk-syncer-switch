using System.IO.Compression;
using System.Text;
using TotkSync.Core.Zip;

namespace TotkSync.Tests.Support;

public static class ZipBuilder
{
    public static (string Name, string Content)[] MinimalEntries => [("option.sav", "option"), ("slot_00/progress.sav", "progress")];

    public static byte[] Build(params (string Name, string Content)[] entries)
    {
        return BuildBytes(entries.Select(entry => (entry.Name, Encoding.UTF8.GetBytes(entry.Content))).ToArray());
    }

    public static byte[] BuildBytes(params (string Name, byte[] Content)[] entries)
    {
        using var output = new MemoryStream();
        using (var archive = new ZipArchive(output, ZipArchiveMode.Create, leaveOpen: true))
        {
            foreach (var (name, content) in entries)
            {
                var entry = archive.CreateEntry(name, CompressionLevel.Optimal);
                using var stream = entry.Open();
                stream.Write(content);
            }
        }
        return output.ToArray();
    }

    public static ValidatedSaveZip Valid(params (string Name, string Content)[] entries)
    {
        return Unwrap(SaveZipValidator.Validate(Build(entries)));
    }

    public static ValidatedSaveZip Unwrap(ZipValidation validation)
    {
        return Assert.IsType<ZipValidation.Valid>(validation).Zip;
    }

    public static byte[] ContractZip(string fileName)
    {
        return File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "contracts", "save-zip", fileName));
    }

    public static (string Name, string Content)[] With(params (string Name, string Content)[] extra)
    {
        return [.. MinimalEntries, .. extra];
    }
}
