using TotkSync.Core.Saves;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SaveSnapshotTests
{
    [Fact]
    public void Snapshot_of_contract_tree_has_contract_fingerprint()
    {
        var snapshot = SaveSnapshot.Capture(SaveTreeBuilder.ContractTreeDir);

        Assert.Equal(SaveTreeBuilder.ContractFingerprint, snapshot.Fingerprint);
    }

    [Fact]
    public void Snapshot_counts_files_and_sums_sizes()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.WriteFile(temp.Path, "option.sav", "12345");
        SaveTreeBuilder.WriteFile(temp.Path, "slot_00/progress.sav", "123");

        var snapshot = SaveSnapshot.Capture(temp.Path);

        Assert.Equal((2, 8L), (snapshot.Files, snapshot.Bytes));
    }

    [Fact]
    public void Snapshot_modified_is_the_latest_file_write_time()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildMinimalSave(temp.Path);
        var older = new DateTime(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        var newer = new DateTime(2026, 3, 5, 10, 30, 0, DateTimeKind.Utc);
        File.SetLastWriteTimeUtc(temp.Combine("option.sav"), older);
        File.SetLastWriteTimeUtc(temp.Combine("slot_00", "progress.sav"), newer);

        Assert.Equal(new DateTimeOffset(newer, TimeSpan.Zero), SaveSnapshot.Capture(temp.Path).Modified);
    }

    [Fact]
    public void Junk_files_change_neither_count_size_date_nor_fingerprint()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildFullSave(temp.Path);
        var old = new DateTime(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        foreach (var file in Directory.EnumerateFiles(temp.Path, "*", SearchOption.AllDirectories))
        {
            File.SetLastWriteTimeUtc(file, old);
        }
        var before = SaveSnapshot.Capture(temp.Path);

        foreach (var junk in new[]
        {
            "desktop.ini", "Thumbs.db", "album/THUMBS.DB", ".nx_save_meta.bin", "a.zip", "b.7z", "c.rar", "slot_01/D.ZIP",
        })
        {
            SaveTreeBuilder.WriteFile(temp.Path, junk, "junk junk junk");
        }

        Assert.Equal(before, SaveSnapshot.Capture(temp.Path));
    }

    [Fact]
    public void Snapshot_of_empty_folder_has_zero_files_and_zero_bytes()
    {
        using var temp = new TempDir();

        var snapshot = SaveSnapshot.Capture(temp.Path);

        Assert.Equal((0, 0L), (snapshot.Files, snapshot.Bytes));
    }

    [Fact]
    public void Snapshot_fingerprint_changes_when_save_content_changes()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildMinimalSave(temp.Path);
        var before = SaveSnapshot.Capture(temp.Path).Fingerprint;

        SaveTreeBuilder.WriteFile(temp.Path, "option.sav", "changed");

        Assert.NotEqual(before, SaveSnapshot.Capture(temp.Path).Fingerprint);
    }
}
