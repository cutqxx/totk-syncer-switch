using System.Security.Cryptography;
using System.Text;
using TotkSync.Core.Saves;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class FingerprintTests
{
    [Fact]
    public void Fingerprint_of_contract_tree_matches_contract_vector()
    {
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, Fingerprint.OfDirectory(SaveTreeBuilder.ContractTreeDir));
    }

    [Fact]
    public void Fingerprint_changes_when_one_byte_of_a_save_file_changes()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildFullSave(temp.Path);
        var before = Fingerprint.OfDirectory(temp.Path);

        var progress = temp.Combine("slot_03", "progress.sav");
        var bytes = File.ReadAllBytes(progress);
        bytes[0] ^= 0x01;
        File.WriteAllBytes(progress, bytes);

        Assert.NotEqual(before, Fingerprint.OfDirectory(temp.Path));
    }

    [Fact]
    public void Fingerprint_changes_when_a_file_is_renamed_with_same_content()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.WriteFile(temp.Path, "a.sav", "same");
        var before = Fingerprint.OfDirectory(temp.Path);

        File.Move(temp.Combine("a.sav"), temp.Combine("b.sav"));

        Assert.NotEqual(before, Fingerprint.OfDirectory(temp.Path));
    }

    [Fact]
    public void Fingerprint_ignores_empty_directories()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildMinimalSave(temp.Path);
        var before = Fingerprint.OfDirectory(temp.Path);

        Directory.CreateDirectory(temp.Combine("album"));

        Assert.Equal(before, Fingerprint.OfDirectory(temp.Path));
    }

    [Fact]
    public void Fingerprint_does_not_depend_on_input_order()
    {
        var files = Entries("b.sav", "a.sav", "c/d.sav");

        Assert.Equal(Fingerprint.Compute(files), Fingerprint.Compute(files.AsEnumerable().Reverse()));
    }

    [Fact]
    public void Fingerprint_orders_paths_by_utf8_bytes_for_punctuation_and_case()
    {
        var expectedOrder = new[] { "B.dat", "a-b.dat", "a.dat", "a/b.dat", "a_b.dat" };

        var actual = Fingerprint.Compute(Entries(expectedOrder.Reverse().ToArray()));

        Assert.Equal(ExpectedFingerprint(expectedOrder), actual);
    }

    [Fact]
    public void Fingerprint_orders_non_ascii_paths_by_utf8_bytes_not_utf16_units()
    {
        // U+FF21 is EF BC A1 in UTF-8 (sorts before F0...), but U+1F600 is the surrogate pair D83D in UTF-16 (sorts before FF21).
        var expectedOrder = new[] { "é.sav", "Ａ.sav", "\U0001F600.sav" };

        var actual = Fingerprint.Compute(Entries(expectedOrder.Reverse().ToArray()));

        Assert.Equal(ExpectedFingerprint(expectedOrder), actual);
    }

    [Fact]
    public void Fingerprint_of_no_files_is_sha256_of_empty_input()
    {
        var expected = "sha256:" + Convert.ToHexStringLower(SHA256.HashData([]));

        Assert.Equal(expected, Fingerprint.Compute([]));
    }

    private static List<(string, byte[])> Entries(params string[] paths)
    {
        return paths.Select(path => (path, ContentHash(path))).ToList();
    }

    private static byte[] ContentHash(string path) => SHA256.HashData(Encoding.UTF8.GetBytes("content:" + path));

    private static string ExpectedFingerprint(string[] pathsInExpectedOrder)
    {
        var text = new StringBuilder();
        foreach (var path in pathsInExpectedOrder)
        {
            text.Append(path).Append('\n').Append(Convert.ToHexStringLower(ContentHash(path))).Append('\n');
        }
        return "sha256:" + Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(text.ToString())));
    }
}
