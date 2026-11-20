using System.Net;
using System.Text.Json;
using TotkSync.Core.Pairing;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class SwitchConfigFileTests : IDisposable
{
    private const string FakeToken = "fake-token-0123456789abcdef";
    private static readonly IPAddress LanIp = IPAddress.Parse("192.168.1.10");

    private readonly TempDir _temp = new();

    public void Dispose() => _temp.Dispose();

    [Fact]
    public void Render_with_the_example_values_matches_the_contract_example_file()
    {
        var examplePath = Path.Combine(AppContext.BaseDirectory, "contracts", "switch-config.example.json");
        var example = File.ReadAllText(examplePath);
        var exampleToken = JsonDocument.Parse(example).RootElement.GetProperty("token").GetString()!;

        var rendered = SwitchConfigFile.Render(LanIp, 8765, exampleToken);

        Assert.Equal(example.ReplaceLineEndings("\n"), rendered);
    }

    [Fact]
    public void Render_uses_two_space_indent_lf_and_a_trailing_newline()
    {
        var rendered = SwitchConfigFile.Render(LanIp, 8765, FakeToken);

        Assert.Equal(
            "{\n  \"pc_host\": \"192.168.1.10\",\n  \"pc_port\": 8765,\n  \"token\": \"" + FakeToken + "\",\n  \"keep_backups\": 20\n}\n",
            rendered);
    }

    [Fact]
    public void Render_writes_the_port_as_a_number_and_the_default_keep_backups()
    {
        var root = JsonDocument.Parse(SwitchConfigFile.Render(LanIp, 9000, FakeToken)).RootElement;

        Assert.Equal(9000, root.GetProperty("pc_port").GetInt32());
        Assert.Equal(20, root.GetProperty("keep_backups").GetInt32());
    }

    [Fact]
    public void Render_converts_an_ipv4_mapped_address_to_plain_ipv4()
    {
        var mapped = LanIp.MapToIPv6();

        var root = JsonDocument.Parse(SwitchConfigFile.Render(mapped, 8765, FakeToken)).RootElement;

        Assert.Equal("192.168.1.10", root.GetProperty("pc_host").GetString());
    }

    [Theory]
    [InlineData("8.8.8.8")]
    [InlineData("169.254.1.1")]
    [InlineData("172.32.0.1")]
    [InlineData("::1")]
    [InlineData("127.0.0.1")]
    [InlineData("127.1.2.3")]
    [InlineData("::ffff:127.0.0.1")]
    [InlineData("fe80::1")]
    public void Render_rejects_a_non_private_address_and_the_message_has_only_the_address(string address)
    {
        var ip = IPAddress.Parse(address);

        var failure = Assert.Throws<ArgumentException>(() => SwitchConfigFile.Render(ip, 8765, FakeToken));

        Assert.Contains(address, failure.Message);
        Assert.DoesNotContain(FakeToken, failure.Message);
    }

    [Fact]
    public void Save_writes_the_rendered_content_and_returns_no_error()
    {
        var path = _temp.Combine("switch-config.json");

        var error = SwitchConfigFile.Save(path, LanIp, 8765, FakeToken);

        Assert.Null(error);
        Assert.Equal(SwitchConfigFile.Render(LanIp, 8765, FakeToken), File.ReadAllText(path));
    }

    [Fact]
    public void Save_writes_utf8_without_a_byte_order_mark()
    {
        var path = _temp.Combine("switch-config.json");

        SwitchConfigFile.Save(path, LanIp, 8765, FakeToken);

        Assert.Equal((byte)'{', File.ReadAllBytes(path)[0]);
    }

    [Fact]
    public void Save_leaves_no_temporary_file_behind()
    {
        var path = _temp.Combine("out", "switch-config.json");

        SwitchConfigFile.Save(path, LanIp, 8765, FakeToken);

        Assert.Equal([path], Directory.GetFiles(_temp.Combine("out")));
    }

    [Fact]
    public void Save_replaces_an_existing_longer_file_entirely()
    {
        var path = _temp.Combine("switch-config.json");
        File.WriteAllText(path, new string('x', 5000));

        var error = SwitchConfigFile.Save(path, LanIp, 8765, FakeToken);

        Assert.Null(error);
        Assert.Equal(SwitchConfigFile.Render(LanIp, 8765, FakeToken), File.ReadAllText(path));
    }

    [Fact]
    public void Save_to_a_path_occupied_by_a_directory_returns_an_error_without_the_token()
    {
        var path = _temp.Combine("switch-config.json");
        Directory.CreateDirectory(path);

        var error = SwitchConfigFile.Save(path, LanIp, 8765, FakeToken);

        Assert.StartsWith("Couldn't save switch-config.json:", error);
        Assert.DoesNotContain(FakeToken, error);
        Assert.True(Directory.Exists(path));
        Assert.Empty(Directory.GetFiles(_temp.Path, "*.tmp"));
    }

    [Fact]
    public void Save_below_a_regular_file_returns_an_error_without_the_token()
    {
        var blocker = _temp.Combine("blocker");
        File.WriteAllText(blocker, "not a directory");

        var error = SwitchConfigFile.Save(Path.Combine(blocker, "switch-config.json"), LanIp, 8765, FakeToken);

        Assert.StartsWith("Couldn't save switch-config.json:", error);
        Assert.DoesNotContain(FakeToken, error);
        Assert.Empty(Directory.GetFiles(_temp.Path, "*.tmp"));
    }

    [Fact]
    public void Save_over_a_read_only_file_returns_an_error_without_the_token_and_leaves_no_temporary_file()
    {
        var path = _temp.Combine("switch-config.json");
        File.WriteAllText(path, "old");
        File.SetAttributes(path, FileAttributes.ReadOnly);
        try
        {
            var error = SwitchConfigFile.Save(path, LanIp, 8765, FakeToken);

            Assert.StartsWith("Couldn't save switch-config.json:", error);
            Assert.DoesNotContain(FakeToken, error);
            Assert.Equal("old", File.ReadAllText(path));
            Assert.Empty(Directory.GetFiles(_temp.Path, "*.tmp"));
        }
        finally
        {
            File.SetAttributes(path, FileAttributes.Normal);
        }
    }

    [Fact]
    public void Save_creates_missing_parent_directories()
    {
        var path = _temp.Combine("a", "b", "switch-config.json");

        var error = SwitchConfigFile.Save(path, LanIp, 8765, FakeToken);

        Assert.Null(error);
        Assert.True(File.Exists(path));
    }

    [Fact]
    public void Save_with_a_public_address_throws_and_writes_nothing()
    {
        var path = _temp.Combine("switch-config.json");

        Assert.Throws<ArgumentException>(() => SwitchConfigFile.Save(path, IPAddress.Parse("8.8.8.8"), 8765, FakeToken));

        Assert.False(File.Exists(path));
    }
}
