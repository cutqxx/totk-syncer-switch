using System.Net;
using System.Text;
using System.Text.Json;
using TotkSync.Core.Settings;

namespace TotkSync.Core.Pairing;

public static class SwitchConfigFile
{
    public const string FileName = "switch-config.json";
    public const string SdCardPath = "sd:/config/totk-sync/config.json";

    public static string Render(IPAddress host, int port, string token)
    {
        var ipv4 = RequirePrivateIpv4(host);
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true, NewLine = "\n" }))
        {
            writer.WriteStartObject();
            writer.WriteString("pc_host", ipv4.ToString());
            writer.WriteNumber("pc_port", port);
            writer.WriteString("token", token);
            writer.WriteNumber("keep_backups", LauncherSettings.DefaultKeepBackups);
            writer.WriteEndObject();
        }
        return Encoding.UTF8.GetString(stream.ToArray()) + "\n";
    }

    public static string? Save(string path, IPAddress host, int port, string token)
    {
        var content = Render(host, port, token);
        try
        {
            AtomicFile.WriteAllBytes(path, new UTF8Encoding(false).GetBytes(content));
            return null;
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException)
        {
            return $"Couldn't save {FileName}: {failure.Message}";
        }
    }

    private static IPAddress RequirePrivateIpv4(IPAddress host)
    {
        var ipv4 = host.IsIPv4MappedToIPv6 ? host.MapToIPv4() : host;
        if (!PrivateNetwork.IsPrivate(ipv4) || IPAddress.IsLoopback(ipv4))
        {
            throw new ArgumentException($"{host} is not a private LAN IPv4 address", nameof(host));
        }
        return ipv4;
    }
}
