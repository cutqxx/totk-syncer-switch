using TotkSync.Core.Pairing;

namespace TotkSync.Tests.Core;

public sealed class FirewallCommandTests
{
    [Theory]
    [InlineData(8765)]
    [InlineData(1024)]
    [InlineData(65535)]
    public void Build_gives_the_exact_netsh_command_for_the_port(int port)
    {
        Assert.Equal(
            $"netsh advfirewall firewall add rule name=\"TotK Save Sync\" dir=in action=allow protocol=TCP localport={port} profile=private",
            FirewallCommand.Build(port));
    }

    [Fact]
    public void Build_limits_the_rule_to_the_private_profile()
    {
        Assert.EndsWith("profile=private", FirewallCommand.Build(8765));
    }
}
