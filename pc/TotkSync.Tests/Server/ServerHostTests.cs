using System.Net;
using System.Net.Sockets;
using TotkSync.Core.Activity;
using TotkSync.Core.Settings;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class ServerHostTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    private readonly TempDir _temp = new();
    private readonly List<ServerHost> _hosts = [];

    public Task InitializeAsync() => Task.CompletedTask;

    public async Task DisposeAsync()
    {
        foreach (var host in _hosts)
        {
            await host.StopAsync();
        }
        _temp.Dispose();
    }

    private static int FreePort()
    {
        var probe = new TcpListener(IPAddress.Loopback, 0);
        probe.Start();
        var port = ((IPEndPoint)probe.LocalEndpoint).Port;
        probe.Stop();
        return port;
    }

    private async Task<ServerHost> NewHostAsync(int port, IPAddress listenAddress)
    {
        var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave: true);
        var session = new SettingsSession(
            new SettingsStore(_temp.Combine("settings.json")),
            new LauncherSettings(Port: port, Token: FakeToken));
        var host = new ServerHost(
            session,
            rig.Query,
            new ActivityLog(_temp.Combine("activity.jsonl")),
            TimeProvider.System,
            listenAddress,
            () => [IPAddress.Parse("192.168.1.10")]);
        _hosts.Add(host);
        return host;
    }

    [Fact]
    public async Task Free_port_from_settings_gives_running_state_and_serves_status_with_the_settings_token()
    {
        var port = FreePort();
        var host = await NewHostAsync(port, IPAddress.Any);

        await host.StartAsync();

        Assert.Equal((ServerPhase.Running, port), (host.State.Phase, host.State.Port));
        Assert.Equal([IPAddress.Parse("192.168.1.10")], host.State.Addresses);
        using var client = new HttpClient(new HttpClientHandler { UseProxy = false });
        using var request = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{port}/status");
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        using var response = await client.SendAsync(request);
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public async Task Port_held_by_someone_else_gives_port_busy_state_with_the_exact_text()
    {
        var blocker = new TcpListener(IPAddress.Any, 0);
        blocker.Start();
        try
        {
            var port = ((IPEndPoint)blocker.LocalEndpoint).Port;
            var host = await NewHostAsync(port, IPAddress.Any);

            await host.StartAsync();

            Assert.Equal(ServerPhase.PortBusy, host.State.Phase);
            Assert.Equal($"Error: port {port} is busy (is the launcher already running?)", host.State.Message);
        }
        finally
        {
            blocker.Stop();
        }
    }

    [Fact]
    public async Task Unbindable_address_gives_failed_state_without_the_token_in_the_text()
    {
        var port = FreePort();
        var host = await NewHostAsync(port, IPAddress.Parse("203.0.113.1"));

        await host.StartAsync();

        Assert.Equal(ServerPhase.Failed, host.State.Phase);
        Assert.StartsWith($"Error: can't listen on port {port} (", host.State.Message);
        Assert.DoesNotContain(FakeToken, host.State.Message);
        Assert.Empty(host.State.Addresses);
    }

    [Fact]
    public async Task Stop_after_a_running_start_releases_the_port()
    {
        var port = FreePort();
        var host = await NewHostAsync(port, IPAddress.Any);
        await host.StartAsync();

        await host.StopAsync();

        var probe = new TcpListener(IPAddress.Any, port);
        probe.Start();
        probe.Stop();
    }

    [Fact]
    public void Error_texts_are_exact()
    {
        Assert.Equal("Error: port 8765 is busy (is the launcher already running?)", ServerErrorText.PortBusy(8765));
        Assert.Equal("Error: can't listen on port 9000 (access denied)", ServerErrorText.CannotListen(9000, "access denied"));
    }
}
