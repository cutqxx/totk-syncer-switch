using System.Net;
using System.Net.Sockets;

namespace TotkSync.Core.Pairing;

public static class PrivateNetwork
{
    public static bool IsPrivate(IPAddress address)
    {
        var ipv4 = address.IsIPv4MappedToIPv6 ? address.MapToIPv4() : address;
        if (ipv4.AddressFamily != AddressFamily.InterNetwork)
        {
            return false;
        }
        var octets = ipv4.GetAddressBytes();
        return octets[0] is 10 or 127
            || (octets[0] == 172 && octets[1] is >= 16 and <= 31)
            || (octets[0] == 192 && octets[1] == 168);
    }
}
