using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace TotkSync.Core.Pairing;

public sealed record AdapterAddress(string AdapterName, IPAddress Address, bool HasGateway);

public static class LanAddresses
{
    private static readonly string[] VirtualAdapterMarkers = ["vEthernet", "WSL", "Hyper-V", "VirtualBox", "VMware", "Docker"];

    public static IReadOnlyList<IPAddress> Filter(IEnumerable<AdapterAddress> candidates)
    {
        return candidates
            .Where(candidate => IsUsable(candidate.Address))
            .OrderBy(candidate => candidate.HasGateway ? 0 : 1)
            .ThenBy(candidate => IsVirtualAdapter(candidate.AdapterName) ? 1 : 0)
            .Select(candidate => candidate.Address)
            .Distinct()
            .ToList();
    }

    public static IReadOnlyList<IPAddress> Detect()
    {
        return Filter(NetworkInterface.GetAllNetworkInterfaces().SelectMany(ToCandidates));
    }

    private static bool IsUsable(IPAddress address)
    {
        return PrivateNetwork.IsPrivate(address) && !IPAddress.IsLoopback(address);
    }

    private static bool IsVirtualAdapter(string adapterName)
    {
        return VirtualAdapterMarkers.Any(marker => adapterName.Contains(marker, StringComparison.OrdinalIgnoreCase));
    }

    private static IEnumerable<AdapterAddress> ToCandidates(NetworkInterface adapter)
    {
        if (adapter.OperationalStatus != OperationalStatus.Up || adapter.NetworkInterfaceType == NetworkInterfaceType.Loopback)
        {
            return [];
        }
        var properties = adapter.GetIPProperties();
        var hasGateway = properties.GatewayAddresses.Any(IsIpv4Gateway);
        var name = $"{adapter.Name} {adapter.Description}";
        return properties.UnicastAddresses
            .Where(unicast => unicast.Address.AddressFamily == AddressFamily.InterNetwork)
            .Select(unicast => new AdapterAddress(name, unicast.Address, hasGateway));
    }

    private static bool IsIpv4Gateway(GatewayIPAddressInformation gateway)
    {
        return gateway.Address.AddressFamily == AddressFamily.InterNetwork
            && !gateway.Address.Equals(IPAddress.Any);
    }
}
