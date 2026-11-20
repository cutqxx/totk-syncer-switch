using System.Net;
using TotkSync.Core.Pairing;

namespace TotkSync.Tests.Core;

public class LanAddressesTests
{
    private static AdapterAddress Adapter(string name, string address, bool hasGateway = true)
    {
        return new AdapterAddress(name, IPAddress.Parse(address), hasGateway);
    }

    private static string[] Filtered(params AdapterAddress[] candidates)
    {
        return LanAddresses.Filter(candidates).Select(address => address.ToString()).ToArray();
    }

    [Fact]
    public void Public_loopback_and_link_local_addresses_are_dropped()
    {
        var result = Filtered(
            Adapter("Ethernet", "8.8.8.8"),
            Adapter("Loop", "127.0.0.1"),
            Adapter("Auto", "169.254.3.4"),
            Adapter("Ethernet", "192.168.1.10"));

        Assert.Equal(["192.168.1.10"], result);
    }

    [Fact]
    public void Adapter_with_gateway_comes_before_adapter_without()
    {
        var result = Filtered(
            Adapter("Isolated", "10.1.1.1", hasGateway: false),
            Adapter("Ethernet", "192.168.1.10", hasGateway: true));

        Assert.Equal(["192.168.1.10", "10.1.1.1"], result);
    }

    [Fact]
    public void Virtual_adapters_come_after_physical_ones_with_the_same_gateway_state()
    {
        var result = Filtered(
            Adapter("vEthernet (WSL)", "172.20.0.1"),
            Adapter("Docker Desktop", "172.17.0.1"),
            Adapter("Wi-Fi", "192.168.1.10"));

        Assert.Equal(["192.168.1.10", "172.20.0.1", "172.17.0.1"], result);
    }

    [Fact]
    public void Duplicate_addresses_are_listed_once()
    {
        var result = Filtered(Adapter("A", "192.168.1.10"), Adapter("B", "192.168.1.10"));

        Assert.Equal(["192.168.1.10"], result);
    }

    [Fact]
    public void No_candidates_gives_empty_list()
    {
        Assert.Empty(LanAddresses.Filter([]));
    }
}
