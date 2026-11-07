using TotkSync.Core.Activity;
using TotkSync.Core.Settings;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class SecretMaskingTests
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    [Fact]
    public void Launcher_settings_to_string_masks_the_token_but_keeps_other_fields()
    {
        var text = new LauncherSettings(GameDir: @"D:\game", Port: 9000, Token: FakeToken).ToString();

        Assert.DoesNotContain(FakeToken, text);
        Assert.Contains("Token = ***", text);
        Assert.Contains(@"GameDir = D:\game", text);
        Assert.Contains("Port = 9000", text);
    }

    [Fact]
    public async Task Api_deps_to_string_masks_the_token()
    {
        using var temp = new TempDir();
        var rig = await StatusRig.CreateAsync(temp.Combine("rig"), withSave: false);

        var text = new ApiDeps(FakeToken, rig.Query, new ActivityLog(temp.Combine("a.jsonl")), TimeProvider.System).ToString();

        Assert.DoesNotContain(FakeToken, text);
        Assert.Contains("Token = ***", text);
    }
}
