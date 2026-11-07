using System.Net;
using TotkSync.Core.Pairing;

namespace TotkSync.Tests.Core;

public class PrivateNetworkTests
{
    [Theory]
    [InlineData("10.0.0.0")]
    [InlineData("10.255.255.255")]
    [InlineData("172.16.0.0")]
    [InlineData("172.31.255.255")]
    [InlineData("192.168.0.0")]
    [InlineData("192.168.255.255")]
    [InlineData("127.0.0.1")]
    [InlineData("127.255.255.255")]
    public void Private_and_loopback_ranges_are_private(string address)
    {
        Assert.True(PrivateNetwork.IsPrivate(IPAddress.Parse(address)));
    }

    [Theory]
    [InlineData("9.255.255.255")]
    [InlineData("11.0.0.0")]
    [InlineData("172.15.255.255")]
    [InlineData("172.32.0.0")]
    [InlineData("192.167.255.255")]
    [InlineData("192.169.0.0")]
    [InlineData("126.255.255.255")]
    [InlineData("128.0.0.0")]
    [InlineData("8.8.8.8")]
    [InlineData("169.254.1.1")]
    [InlineData("100.64.0.1")]
    [InlineData("0.0.0.0")]
    [InlineData("255.255.255.255")]
    public void Addresses_just_outside_private_ranges_and_public_ones_are_not_private(string address)
    {
        Assert.False(PrivateNetwork.IsPrivate(IPAddress.Parse(address)));
    }

    [Theory]
    [InlineData("::ffff:192.168.1.10", true)]
    [InlineData("::ffff:10.0.0.1", true)]
    [InlineData("::ffff:127.0.0.1", true)]
    [InlineData("::ffff:172.32.0.1", false)]
    [InlineData("::ffff:8.8.8.8", false)]
    public void Ipv4_mapped_ipv6_is_judged_by_its_ipv4_address(string address, bool expected)
    {
        Assert.Equal(expected, PrivateNetwork.IsPrivate(IPAddress.Parse(address)));
    }

    [Theory]
    [InlineData("::1")]
    [InlineData("fe80::1")]
    [InlineData("fd00::1")]
    [InlineData("2001:db8::1")]
    public void Pure_ipv6_addresses_are_not_private_even_loopback(string address)
    {
        Assert.False(PrivateNetwork.IsPrivate(IPAddress.Parse(address)));
    }
}
