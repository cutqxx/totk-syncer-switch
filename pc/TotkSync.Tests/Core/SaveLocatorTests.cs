using TotkSync.Core.Saves;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SaveLocatorTests
{
    [Fact]
    public void Single_valid_folder_under_profile_is_found()
    {
        using var temp = new TempDir();
        var expected = Path.Combine(temp.Path, "profile", "0100ABC");
        SaveTreeBuilder.BuildGameDirWithSave(temp.Path, "0100ABC");

        var result = SaveLocator.Locate(temp.Path, null);

        Assert.Equal(new LocateResult.Found(expected), result);
    }

    [Fact]
    public void Missing_profile_directory_gives_not_found()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.WriteFile(temp.Path, "totk.exe", "exe");

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(temp.Path, null));
    }

    [Fact]
    public void Empty_profile_directory_gives_not_found()
    {
        using var temp = new TempDir();
        Directory.CreateDirectory(temp.Combine("profile"));

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(temp.Path, null));
    }

    [Fact]
    public void Folder_with_option_but_no_slot_progress_is_not_a_candidate()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.WriteFile(temp.Path, "profile/A/option.sav", "o");
        SaveTreeBuilder.WriteFile(temp.Path, "profile/A/slot_00/caption.sav", "c");

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(temp.Path, null));
    }

    [Fact]
    public void Folder_with_slot_progress_but_no_option_is_not_a_candidate()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.WriteFile(temp.Path, "profile/A/slot_00/progress.sav", "p");

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(temp.Path, null));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(5)]
    public void Any_slot_from_zero_to_five_makes_a_folder_a_candidate(int slot)
    {
        using var temp = new TempDir();
        SaveTreeBuilder.WriteFile(temp.Path, "profile/A/option.sav", "o");
        SaveTreeBuilder.WriteFile(temp.Path, $"profile/A/slot_0{slot}/progress.sav", "p");

        Assert.IsType<LocateResult.Found>(SaveLocator.Locate(temp.Path, null));
    }

    [Fact]
    public void Slot_number_outside_zero_to_five_does_not_count()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.WriteFile(temp.Path, "profile/A/option.sav", "o");
        SaveTreeBuilder.WriteFile(temp.Path, "profile/A/slot_06/progress.sav", "p");

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(temp.Path, null));
    }

    [Fact]
    public void Save_nested_deeper_than_direct_child_of_profile_is_ignored()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildMinimalSave(temp.Combine("profile", "outer", "inner"));

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(temp.Path, null));
    }

    [Fact]
    public void Save_files_directly_in_profile_are_ignored()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildMinimalSave(temp.Combine("profile"));

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(temp.Path, null));
    }

    [Fact]
    public void Several_valid_folders_are_all_reported_and_none_is_chosen()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildGameDirWithSave(temp.Path, "B");
        SaveTreeBuilder.BuildMinimalSave(temp.Combine("profile", "A"));
        SaveTreeBuilder.WriteFile(temp.Path, "profile/junk-only/readme.txt", "x");

        var result = Assert.IsType<LocateResult.Several>(SaveLocator.Locate(temp.Path, null));

        Assert.Equal(
            [temp.Combine("profile", "A"), temp.Combine("profile", "B")],
            result.Items.Select(item => item.Dir));
    }

    [Fact]
    public void Candidate_modified_time_ignores_junk_files()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildGameDirWithSave(temp.Path, "A");
        SaveTreeBuilder.BuildMinimalSave(temp.Combine("profile", "B"));
        var saveTime = new DateTime(2026, 1, 1, 12, 0, 0, DateTimeKind.Utc);
        File.SetLastWriteTimeUtc(temp.Combine("profile", "A", "option.sav"), saveTime);
        File.SetLastWriteTimeUtc(temp.Combine("profile", "A", "slot_00", "progress.sav"), saveTime);
        SaveTreeBuilder.WriteFile(temp.Path, "profile/A/desktop.ini", "j");
        File.SetLastWriteTimeUtc(temp.Combine("profile", "A", "desktop.ini"), saveTime.AddDays(30));

        var result = Assert.IsType<LocateResult.Several>(SaveLocator.Locate(temp.Path, null));

        var candidate = result.Items.Single(item => item.Dir.EndsWith("A", StringComparison.Ordinal));
        Assert.Equal(new DateTimeOffset(saveTime, TimeSpan.Zero), candidate.Modified);
    }

    [Fact]
    public void Save_dir_override_wins_over_auto_discovery()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildGameDirWithSave(temp.Path, "A");
        SaveTreeBuilder.BuildMinimalSave(temp.Combine("profile", "B"));
        var custom = temp.Combine("custom");
        Directory.CreateDirectory(custom);

        Assert.Equal(new LocateResult.Found(custom), SaveLocator.Locate(temp.Path, custom));
    }

    [Fact]
    public void Save_dir_override_is_used_even_without_profile_directory()
    {
        using var temp = new TempDir();
        var custom = temp.Combine("elsewhere");
        Directory.CreateDirectory(custom);

        Assert.Equal(new LocateResult.Found(custom), SaveLocator.Locate(temp.Combine("no-game"), custom));
    }

    [Fact]
    public void Non_existent_save_dir_override_gives_not_found_even_if_discovery_would_succeed()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildGameDirWithSave(temp.Path, "A");

        Assert.IsType<LocateResult.NotFound>(SaveLocator.Locate(temp.Path, temp.Combine("missing")));
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public void Blank_save_dir_override_falls_back_to_auto_discovery(string blank)
    {
        using var temp = new TempDir();
        SaveTreeBuilder.BuildGameDirWithSave(temp.Path, "A");

        Assert.IsType<LocateResult.Found>(SaveLocator.Locate(temp.Path, blank));
    }

    [Fact]
    public void Not_found_message_matches_spec_text()
    {
        Assert.Equal(
            "PC save not found: start TotK on PC once or set save folder in Settings",
            SaveLocator.NotFoundMessage);
    }
}
