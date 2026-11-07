using TotkSync.Core.Settings;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class AtomicFileTests : IDisposable
{
    private readonly TempDir _temp = new();

    public void Dispose() => _temp.Dispose();

    [Fact]
    public void Write_creates_the_file_and_leaves_no_temporary_file()
    {
        var path = _temp.Combine("sub", "file.bin");

        AtomicFile.WriteAllBytes(path, [1, 2, 3]);

        Assert.Equal([1, 2, 3], File.ReadAllBytes(path));
        Assert.Equal([path], Directory.GetFiles(_temp.Combine("sub")));
    }

    [Fact]
    public void Write_onto_a_directory_throws_and_removes_the_temporary_file()
    {
        var path = _temp.Combine("file.bin");
        Directory.CreateDirectory(path);

        Assert.ThrowsAny<Exception>(() => AtomicFile.WriteAllBytes(path, [1]));

        Assert.Empty(Directory.GetFiles(_temp.Path));
        Assert.True(Directory.Exists(path));
    }

    [Fact]
    public void Write_over_a_read_only_file_throws_keeps_the_old_content_and_removes_the_temporary_file()
    {
        var path = _temp.Combine("file.bin");
        File.WriteAllBytes(path, [9]);
        File.SetAttributes(path, FileAttributes.ReadOnly);
        try
        {
            Assert.ThrowsAny<Exception>(() => AtomicFile.WriteAllBytes(path, [1]));

            Assert.Equal([9], File.ReadAllBytes(path));
            Assert.Equal([path], Directory.GetFiles(_temp.Path));
        }
        finally
        {
            File.SetAttributes(path, FileAttributes.Normal);
        }
    }
}
