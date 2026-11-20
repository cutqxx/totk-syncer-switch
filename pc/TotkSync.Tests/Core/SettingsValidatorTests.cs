using TotkSync.Core.Settings;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SettingsValidatorTests
{
    [Fact]
    public void Game_dir_with_totk_exe_is_accepted()
    {
        using var temp = new TempDir();
        SaveTreeBuilder.WriteFile(temp.Path, "totk.exe", "exe");

        Assert.Null(SettingsValidator.GameDirError(temp.Path));
    }

    [Fact]
    public void Game_dir_without_totk_exe_reports_path_in_message()
    {
        using var temp = new TempDir();

        Assert.Equal($"totk.exe not found in {temp.Path}", SettingsValidator.GameDirError(temp.Path));
    }

    [Fact]
    public void Non_existent_game_dir_is_rejected()
    {
        var path = Path.Combine(Path.GetTempPath(), "totksync-missing-" + Guid.NewGuid().ToString("N"));

        Assert.Equal($"totk.exe not found in {path}", SettingsValidator.GameDirError(path));
    }

    [Fact]
    public void Directory_named_totk_exe_is_not_accepted()
    {
        using var temp = new TempDir();
        Directory.CreateDirectory(temp.Combine("totk.exe"));

        Assert.NotNull(SettingsValidator.GameDirError(temp.Path));
    }

    [Theory]
    [InlineData(1024)]
    [InlineData(8765)]
    [InlineData(65535)]
    public void Ports_in_range_are_accepted(int port)
    {
        Assert.Null(SettingsValidator.PortError(port));
    }

    [Theory]
    [InlineData(1023)]
    [InlineData(0)]
    [InlineData(-1)]
    [InlineData(65536)]
    public void Ports_out_of_range_are_rejected_with_message(int port)
    {
        Assert.Equal("Port must be between 1024 and 65535.", SettingsValidator.PortError(port));
    }

    [Theory]
    [InlineData(1)]
    [InlineData(20)]
    public void Keep_backups_of_one_or_more_is_accepted(int value)
    {
        Assert.Null(SettingsValidator.KeepBackupsError(value));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-5)]
    public void Keep_backups_below_one_is_rejected_with_message(int value)
    {
        Assert.Equal("Keep at least 1 backup.", SettingsValidator.KeepBackupsError(value));
    }
}
