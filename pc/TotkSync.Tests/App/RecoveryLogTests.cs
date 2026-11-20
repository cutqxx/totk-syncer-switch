using TotkSync.App.Services;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.App;

public class RecoveryLogTests : IDisposable
{
    private readonly TempDir _temp = new();

    public void Dispose() => _temp.Dispose();

    [Fact]
    public void Default_title_is_Startup_recovery()
    {
        var path = _temp.Combine("logs", "error.log");

        RecoveryLog.Append(path, ["note one"]);

        var text = File.ReadAllText(path);
        Assert.Contains("Startup recovery:\nnote one\n", text);
    }

    [Fact]
    public void Given_title_is_written_instead_of_the_default()
    {
        var path = _temp.Combine("error.log");

        RecoveryLog.Append(path, ["note"], "Deferred recovery");

        var text = File.ReadAllText(path);
        Assert.Contains("Deferred recovery:\nnote\n", text);
        Assert.DoesNotContain("Startup recovery", text);
    }

    [Fact]
    public void No_notes_writes_nothing()
    {
        var path = _temp.Combine("error.log");

        RecoveryLog.Append(path, []);

        Assert.False(File.Exists(path));
    }
}
