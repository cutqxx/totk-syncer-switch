using System.Net;

namespace TotkSync.App.Services;

public sealed record ServerAddress(IPAddress Ip, string Url)
{
    public static ServerAddress For(IPAddress ip, int port) => new(ip, $"http://{ip}:{port}");
}
