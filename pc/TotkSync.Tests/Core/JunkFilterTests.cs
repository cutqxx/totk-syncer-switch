using TotkSync.Core.Saves;

namespace TotkSync.Tests.Core;

public class JunkFilterTests
{
    [Theory]
    [InlineData("desktop.ini")]
    [InlineData("DESKTOP.INI")]
    [InlineData("Thumbs.db")]
    [InlineData("THUMBS.DB")]
    [InlineData("picturebook/THUMBS.DB")]
    [InlineData("album\\sub\\thumbs.db")]
    [InlineData(".nx_save_meta.bin")]
    [InlineData(".NX_SAVE_META.BIN")]
    [InlineData("x.zip")]
    [InlineData("backup.ZIP")]
    [InlineData("old/save.7z")]
    [InlineData("save.RaR")]
    public void Junk_files_are_recognised(string relPath)
    {
        Assert.True(JunkFilter.IsJunk(relPath));
    }

    [Theory]
    [InlineData("option.sav")]
    [InlineData("slot_00/progress.sav")]
    [InlineData("storage/CacheStorageKey.dat")]
    [InlineData("zip/data.sav")]
    [InlineData("thumbs.db.sav")]
    [InlineData("mydesktop.ini")]
    [InlineData("archive.zipx")]
    public void Real_save_files_are_not_junk(string relPath)
    {
        Assert.False(JunkFilter.IsJunk(relPath));
    }

    [Theory]
    [InlineData(".nx_save_meta.bin", true)]
    [InlineData("sub/.NX_SAVE_META.BIN", true)]
    [InlineData("desktop.ini", false)]
    [InlineData("option.sav", false)]
    public void Meta_file_is_recognised_by_name_only(string relPath, bool expected)
    {
        Assert.Equal(expected, JunkFilter.IsMeta(relPath));
    }
}
