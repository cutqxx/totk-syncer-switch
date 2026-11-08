using System.IO.Compression;
using System.Text.Json;
using TotkSync.Core.Saves;
using TotkSync.Core.Zip;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class SaveZipContractTests
{
    private const long Mebibyte = 1024 * 1024;

    private static string ZipDir => Path.Combine(AppContext.BaseDirectory, "contracts", "save-zip");

    public static TheoryData<string> ContractFiles()
    {
        var data = new TheoryData<string>();
        foreach (var file in LoadCases().Select(item => item.GetProperty("file").GetString()!))
        {
            data.Add(file);
        }
        return data;
    }

    private static List<JsonElement> LoadCases()
    {
        using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(ZipDir, "cases.json")));
        return document.RootElement.EnumerateArray().Select(item => item.Clone()).ToList();
    }

    private static JsonElement CaseFor(string file) => LoadCases().Single(item => item.GetProperty("file").GetString() == file);

    private static List<string> Strings(JsonElement array) => array.EnumerateArray().Select(item => item.GetString()!).ToList();

    [Fact]
    public void Contract_lists_every_vector_archive_in_the_folder_and_nothing_else()
    {
        var listed = LoadCases().Select(item => item.GetProperty("file").GetString()!).Order(StringComparer.Ordinal).ToList();
        var onDisk = Directory.EnumerateFiles(ZipDir, "*.zip").Select(path => Path.GetFileName(path)).Order(StringComparer.Ordinal).ToList();

        Assert.Equal(34, listed.Count);
        Assert.Equal(onDisk, listed);
    }

    [Theory]
    [MemberData(nameof(ContractFiles))]
    public void Validator_gives_the_verdict_and_reason_from_cases_json(string file)
    {
        var expected = CaseFor(file);

        var verdict = SaveZipValidator.Validate(ZipBuilder.ContractZip(file));

        if (expected.GetProperty("valid").GetBoolean())
        {
            var valid = Assert.IsType<ZipValidation.Valid>(verdict);
            Assert.Equal(Strings(expected.GetProperty("saved_files")).Order(StringComparer.Ordinal), valid.Zip.Files.Select(f => f.RelPath).Order(StringComparer.Ordinal));
            Assert.Equal(Strings(expected.GetProperty("saved_dirs")).Order(StringComparer.Ordinal), valid.Zip.Directories.Order(StringComparer.Ordinal));
        }
        else
        {
            var invalid = Assert.IsType<ZipValidation.Invalid>(verdict);
            Assert.Equal(expected.GetProperty("reason").GetString(), invalid.Reason);
        }
    }

    [Fact]
    public void Valid_save_tree_zip_has_the_contract_fingerprint()
    {
        var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(ZipBuilder.ContractZip("valid-save-tree.zip")));

        Assert.Equal(SaveTreeBuilder.ContractFingerprint, Fingerprint.OfZip(zip));
    }

    [Fact]
    public void Zip_built_by_the_switch_packer_has_the_contract_fingerprint()
    {
        var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(ZipBuilder.ContractZip("valid-from-switch.zip")));

        Assert.Equal(SaveTreeBuilder.ContractFingerprint, Fingerprint.OfZip(zip));
    }

    [Fact]
    public void Jksv_meta_in_the_zip_is_not_part_of_the_saved_content()
    {
        var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(ZipBuilder.ContractZip("valid-jksv-meta.zip")));

        Assert.DoesNotContain(zip.Files, file => JunkFilter.IsMeta(file.RelPath));
    }

    [Theory]
    [InlineData("slot_00/CONIN$")]
    [InlineData("slot_00/CONOUT$")]
    [InlineData("slot_00/conin$.sav")]
    [InlineData("slot_00/CONOUT$.tar.gz")]
    [InlineData("CONIN$/x.sav")]
    [InlineData("slot_00/CON")]
    [InlineData("slot_00/prn.txt")]
    [InlineData("slot_00/Aux.sav")]
    [InlineData("slot_00/nul.tar.gz")]
    [InlineData("slot_00/CON .txt")]
    [InlineData("slot_00/nul  .dat")]
    [InlineData("slot_00/Aux   ")]
    [InlineData("slot_00/COM1")]
    [InlineData("slot_00/com9.sav")]
    [InlineData("slot_00/LPT1.sav")]
    [InlineData("slot_00/lpt9")]
    public void Windows_device_names_in_any_case_and_with_any_extension_are_unsafe(string path)
    {
        var verdict = SaveZipValidator.Validate(ZipBuilder.Build(ZipBuilder.With((path, "x"))));

        Assert.StartsWith("unsafe path: ", Assert.IsType<ZipValidation.Invalid>(verdict).Reason);
    }

    [Theory]
    [InlineData("slot_00/COM10")]
    [InlineData("slot_00/console.sav")]
    [InlineData("slot_00/CONIN.sav")]
    [InlineData("slot_00/my.con")]
    [InlineData("slot_00/LPT0.sav")]
    public void Names_that_merely_resemble_device_names_are_accepted(string path)
    {
        var verdict = SaveZipValidator.Validate(ZipBuilder.Build(ZipBuilder.With((path, "x"))));

        Assert.Contains(path, ZipBuilder.Unwrap(verdict).Files.Select(file => file.RelPath));
    }

    [Theory]
    [InlineData("slot_00/ends-with-dot.")]
    [InlineData("slot_00/ends-with-space ")]
    [InlineData("album./x.dat")]
    [InlineData("album /x.dat")]
    [InlineData("slot_00/ends-with-two-dots..")]
    [InlineData("slot_00/a<b")]
    [InlineData("slot_00/a>b")]
    [InlineData("slot_00/a|b")]
    [InlineData("slot_00/a?b")]
    [InlineData("slot_00/a*b")]
    [InlineData("slot_00/a\"b")]
    [InlineData("slot_00/tab\there")]
    [InlineData("slot_00/del\u007fchar")]
    [InlineData("slot_00//double-slash")]
    [InlineData("./option2.sav")]
    [InlineData("slot_00/./x.sav")]
    public void Characters_and_shapes_that_windows_mangles_are_unsafe(string path)
    {
        var verdict = SaveZipValidator.Validate(ZipBuilder.Build(ZipBuilder.With((path, "x"))));

        Assert.StartsWith("unsafe path: ", Assert.IsType<ZipValidation.Invalid>(verdict).Reason);
    }

    [Fact]
    public void Paths_differing_only_by_case_are_a_duplicate()
    {
        var verdict = SaveZipValidator.Validate(ZipBuilder.Build(ZipBuilder.With(("slot_00/Progress.SAV", "other"))));

        Assert.Equal(SaveZipReasons.DuplicatePath("slot_00/Progress.SAV"), Assert.IsType<ZipValidation.Invalid>(verdict).Reason);
    }

    [Fact]
    public void A_file_and_a_directory_that_differ_only_by_case_conflict()
    {
        var verdict = SaveZipValidator.Validate(ZipBuilder.Build(ZipBuilder.With(("Album", "file"), ("album/x.sav", "x"))));

        Assert.Equal(SaveZipReasons.ConflictingPaths("album/x.sav"), Assert.IsType<ZipValidation.Invalid>(verdict).Reason);
    }

    [Fact]
    public void Option_file_in_different_case_does_not_count_as_option_sav()
    {
        var verdict = SaveZipValidator.Validate(ZipBuilder.Build(("OPTION.SAV", "o"), ("slot_00/progress.sav", "p")));

        Assert.Equal(SaveZipReasons.MissingOption, Assert.IsType<ZipValidation.Invalid>(verdict).Reason);
    }

    [Fact]
    public void Exactly_one_thousand_entries_are_accepted_and_one_more_is_rejected()
    {
        var filler = Enumerable.Range(0, 998).Select(i => ($"picturebook/f{i:0000}.dat", "x")).ToArray();
        var atLimit = ZipBuilder.Build(ZipBuilder.With(filler));
        var overLimit = ZipBuilder.Build(ZipBuilder.With([.. filler, ("picturebook/extra.dat", "x")]));

        Assert.Equal(1000, ZipBuilder.Unwrap(SaveZipValidator.Validate(atLimit)).Files.Count);
        Assert.Equal(SaveZipReasons.TooManyEntries, Assert.IsType<ZipValidation.Invalid>(SaveZipValidator.Validate(overLimit)).Reason);
    }

    [Fact]
    public void Four_mebibytes_of_zeros_is_a_suspicious_compression_ratio()
    {
        var zip = BuildWithZeros(4 * Mebibyte, padding: 0);

        Assert.Equal(SaveZipReasons.SuspiciousCompression, Assert.IsType<ZipValidation.Invalid>(SaveZipValidator.Validate(zip)).Reason);
    }

    [Fact]
    public void Highly_compressible_data_just_below_four_mebibytes_is_not_checked_for_ratio()
    {
        var zip = BuildWithZeros(4 * Mebibyte - 1000, padding: 0);

        var valid = ZipBuilder.Unwrap(SaveZipValidator.Validate(zip));

        Assert.Equal(4 * Mebibyte - 1000, valid.Files.Single(file => file.RelPath == "picturebook/zeros.dat").Content.Length);
    }

    [Fact]
    public void Ratio_of_exactly_200_to_1_is_accepted_and_201_to_1_is_rejected()
    {
        var (at200, at201) = FindRatioBoundary();

        Assert.Equal(200, RatioOf(at200));
        Assert.Equal(201, RatioOf(at201));
        Assert.IsType<ZipValidation.Valid>(SaveZipValidator.Validate(at200));
        Assert.Equal(SaveZipReasons.SuspiciousCompression, Assert.IsType<ZipValidation.Invalid>(SaveZipValidator.Validate(at201)).Reason);
    }

    private static (byte[] At200, byte[] At201) FindRatioBoundary()
    {
        var low = 1;
        var high = 400_000;
        while (high - low > 1)
        {
            var middle = (low + high) / 2;
            if (RatioOf(BuildWithZeros(4 * Mebibyte, middle)) > 200)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
        }
        return (BuildWithZeros(4 * Mebibyte, high), BuildWithZeros(4 * Mebibyte, low));
    }

    private static byte[] BuildWithZeros(long zeroBytes, int padding)
    {
        using var output = new MemoryStream();
        using (var archive = new ZipArchive(output, ZipArchiveMode.Create, leaveOpen: true))
        {
            AddEntry(archive, "option.sav", "option"u8.ToArray(), CompressionLevel.Optimal);
            AddEntry(archive, "slot_00/progress.sav", "progress"u8.ToArray(), CompressionLevel.Optimal);
            AddEntry(archive, "picturebook/zeros.dat", new byte[zeroBytes], CompressionLevel.SmallestSize);
            if (padding > 0)
            {
                AddEntry(archive, "picturebook/random.dat", RandomBytes(padding), CompressionLevel.NoCompression);
            }
        }
        return output.ToArray();
    }

    private static void AddEntry(ZipArchive archive, string name, byte[] content, CompressionLevel level)
    {
        using var stream = archive.CreateEntry(name, level).Open();
        stream.Write(content);
    }

    private static byte[] RandomBytes(int count)
    {
        var bytes = new byte[count];
        new Random(12345).NextBytes(bytes);
        return bytes;
    }

    private static long RatioOf(byte[] zip)
    {
        using var archive = new ZipArchive(new MemoryStream(zip), ZipArchiveMode.Read);
        var unpacked = archive.Entries.Sum(entry => entry.Length);
        var packed = archive.Entries.Sum(entry => entry.CompressedLength);
        return unpacked / packed;
    }

    [Theory]
    [InlineData("slot_00/.nx_save_meta.bin")]
    [InlineData(".NX_SAVE_META.BIN")]
    public void Foreign_or_malformed_meta_in_any_folder_or_case_marks_another_game(string metaPath)
    {
        var verdict = SaveZipValidator.Validate(ZipBuilder.Build(ZipBuilder.With((metaPath, "JKSV"))));

        Assert.Equal(SaveZipReasons.AnotherGame, Assert.IsType<ZipValidation.Invalid>(verdict).Reason);
    }

    [Fact]
    public void Empty_input_is_not_a_zip()
    {
        Assert.Equal(SaveZipReasons.NotAZip, Assert.IsType<ZipValidation.Invalid>(SaveZipValidator.Validate(ReadOnlyMemory<byte>.Empty)).Reason);
    }

    [Fact]
    public void Truncated_valid_zip_is_not_a_zip()
    {
        var whole = ZipBuilder.ContractZip("valid-save-tree.zip");

        var verdict = SaveZipValidator.Validate(whole.AsMemory(0, whole.Length / 2));

        Assert.Equal(SaveZipReasons.NotAZip, Assert.IsType<ZipValidation.Invalid>(verdict).Reason);
    }

    [Fact]
    public void Offset_slice_of_a_larger_buffer_validates_like_the_exact_array()
    {
        var zip = ZipBuilder.ContractZip("valid-minimal.zip");
        var padded = new byte[zip.Length + 20];
        zip.CopyTo(padded, 10);

        var verdict = SaveZipValidator.Validate(padded.AsMemory(10, zip.Length));

        Assert.Equal(["option.sav", "slot_00/progress.sav"], ZipBuilder.Unwrap(verdict).Files.Select(f => f.RelPath).Order(StringComparer.Ordinal));
    }

    [Fact]
    public void Packed_save_validates_back_to_the_same_fingerprint_and_keeps_empty_directories()
    {
        using var temp = new TempDir();
        var saveDir = SaveTreeBuilder.BuildFullSave(temp.Combine("save"));
        SaveTreeBuilder.WriteFile(saveDir, "Thumbs.db", "junk");

        var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(SaveZipPacker.Pack(saveDir)));

        Assert.Equal(Fingerprint.OfDirectory(saveDir), Fingerprint.OfZip(zip));
        Assert.Contains("album", zip.Directories);
        Assert.DoesNotContain(zip.Files, file => file.RelPath == "Thumbs.db");
    }
}
