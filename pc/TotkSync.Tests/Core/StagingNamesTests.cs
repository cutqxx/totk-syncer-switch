using TotkSync.Core.Replace;

namespace TotkSync.Tests.Core;

public sealed class StagingNamesTests
{
    private static readonly string Parent = Path.Combine("C:", "game", "profile");

    [Fact]
    public void Stage_and_old_are_siblings_of_the_save_and_share_one_suffix()
    {
        var saveDir = Path.Combine(Parent, "A");

        var names = StagingNames.For(saveDir);

        Assert.Equal((Parent, Parent), (Path.GetDirectoryName(names.Stage), Path.GetDirectoryName(names.Old)));
        Assert.Matches(@"^A\.totksync-new-[0-9a-f]{8}$", Path.GetFileName(names.Stage));
        Assert.Equal(Path.GetFileName(names.Stage)["A.totksync-new-".Length..], Path.GetFileName(names.Old)["A.totksync-old-".Length..]);
    }

    [Fact]
    public void Trailing_separator_on_the_save_path_does_not_change_the_names_location()
    {
        var names = StagingNames.For(Path.Combine(Parent, "A") + Path.DirectorySeparatorChar);

        Assert.Equal(Parent, Path.GetDirectoryName(names.Stage));
    }

    [Fact]
    public void Every_call_gets_a_fresh_suffix()
    {
        var saveDir = Path.Combine(Parent, "A");

        var stages = Enumerable.Range(0, 50).Select(_ => StagingNames.For(saveDir).Stage).Distinct().ToList();

        Assert.Equal(50, stages.Count);
    }

    [Fact]
    public void Original_of_stage_and_old_is_the_save_folder()
    {
        var saveDir = Path.Combine(Parent, "A");
        var names = StagingNames.For(saveDir);

        Assert.Equal([saveDir, saveDir], [StagingNames.OriginalOf(names.Stage), StagingNames.OriginalOf(names.Old)]);
    }

    [Fact]
    public void Original_of_a_name_that_itself_contains_dots_keeps_everything_before_the_marker()
    {
        var names = StagingNames.For(Path.Combine(Parent, "A.v2"));

        Assert.Equal(Path.Combine(Parent, "A.v2"), StagingNames.OriginalOf(names.Old));
    }

    [Fact]
    public void Only_the_old_name_is_classified_as_old()
    {
        var names = StagingNames.For(Path.Combine(Parent, "A"));

        Assert.Equal([true, false, true, true], [StagingNames.IsOld(names.Old), StagingNames.IsOld(names.Stage), StagingNames.IsLeftover(names.Old), StagingNames.IsLeftover(names.Stage)]);
    }

    [Theory]
    [InlineData("A")]
    [InlineData("A-backup")]
    [InlineData("A.old")]
    [InlineData("totksync-new")]
    [InlineData("A.totksync")]
    [InlineData("notes.totksync-keep")]
    [InlineData("A.totksync-old-0a1b2c3")]
    [InlineData("A.totksync-old-0a1b2c3dd")]
    [InlineData("A.totksync-old-0A1B2C3D")]
    [InlineData("A.totksync-xyz-0a1b2c3d")]
    [InlineData(".totksync-old-0a1b2c3d")]
    public void Ordinary_folder_names_are_not_leftovers(string name)
    {
        Assert.False(StagingNames.IsLeftover(Path.Combine(Parent, name)));
    }

    [Fact]
    public void Bad_name_is_a_leftover_of_the_save_folder_and_neither_old_nor_stage()
    {
        var bad = StagingNames.BadFor(Path.Combine(Parent, "A"));

        Assert.Equal([true, true, false], [StagingNames.IsBad(bad), StagingNames.IsLeftover(bad), StagingNames.IsOld(bad)]);
        Assert.Equal(Path.Combine(Parent, "A"), StagingNames.OriginalOf(bad));
    }

    [Theory]
    [InlineData("A.totksync-old-0a1b2c3d.unverified", true)]
    [InlineData("A.v2.totksync-old-0a1b2c3d.unverified", true)]
    [InlineData("A.totksync-new-0a1b2c3d.unverified", false)]
    [InlineData("A.totksync-old-0a1b2c3d.unverified.txt", false)]
    [InlineData("A.totksync-old-0A1B2C3D.unverified", false)]
    [InlineData(".totksync-old-0a1b2c3d.unverified", false)]
    [InlineData("A.totksync-old-0a1b2c3d", false)]
    public void Only_our_marker_names_are_unverified_markers(string name, bool expected)
    {
        Assert.Equal(expected, StagingNames.IsUnverifiedMarker(Path.Combine(Parent, name)));
    }
}
