using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Activity;
using TotkSync.Core.Settings;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class ServerHostRestartTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    private readonly TempDir _temp = new();
    private readonly List<ServerHost> _hosts = [];
    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(20) };

    public Task InitializeAsync() => Task.CompletedTask;

    public async Task DisposeAsync()
    {
        _client.Dispose();
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

    private static bool IsPortFree(int port)
    {
        try
        {
            var probe = new TcpListener(IPAddress.Any, port);
            probe.Start();
            probe.Stop();
            return true;
        }
        catch (SocketException)
        {
            return false;
        }
    }

    private async Task<(ServerHost Host, SettingsSession Session)> NewHostAsync(int port, IPAddress? listenAddress = null)
    {
        var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave: true);
        var session = new SettingsSession(
            new SettingsStore(_temp.Combine("settings.json")),
            new LauncherSettings(Port: port, Token: FakeToken));
        var host = new ServerHost(
            session,
            rig.Query,
            new ActivityLog(_temp.Combine("activity.jsonl")),
            new FakeTimeProvider(new DateTimeOffset(2026, 11, 7, 10, 0, 0, TimeSpan.Zero)),
            listenAddress ?? IPAddress.Any,
            () => [IPAddress.Parse("192.168.1.10")]);
        _hosts.Add(host);
        return (host, session);
    }

    private async Task<HttpStatusCode> StatusCodeAsync(int port)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{port}/status");
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        using var response = await _client.SendAsync(request);
        return response.StatusCode;
    }

    [Fact]
    public async Task Running_host_moves_to_the_new_port_and_frees_the_old_one()
    {
        var oldPort = FreePort();
        var (host, session) = await NewHostAsync(oldPort);
        await host.StartAsync();
        var newPort = FreePort();
        await session.SetPortAsync(newPort);

        await host.RestartIfPortChangedAsync();

        Assert.Equal((ServerPhase.Running, newPort), (host.State.Phase, host.State.Port));
        Assert.Equal(HttpStatusCode.OK, await StatusCodeAsync(newPort));
        Assert.True(IsPortFree(oldPort));
    }

    [Fact]
    public async Task Unchanged_port_does_not_restart_the_server()
    {
        var port = FreePort();
        var (host, _) = await NewHostAsync(port);
        await host.StartAsync();
        var phases = new List<ServerPhase>();
        host.Changed += () => phases.Add(host.State.Phase);

        await host.RestartIfPortChangedAsync();

        Assert.Empty(phases);
        Assert.Equal(HttpStatusCode.OK, await StatusCodeAsync(port));
    }

    [Fact]
    public async Task Stopped_host_stays_stopped_after_a_port_change()
    {
        var (host, session) = await NewHostAsync(FreePort());
        await host.StartAsync();
        await host.StopAsync();
        var newPort = FreePort();
        await session.SetPortAsync(newPort);

        await host.RestartIfPortChangedAsync();

        Assert.NotEqual(ServerPhase.Running, host.State.Phase);
        Assert.True(IsPortFree(newPort));
    }

    [Fact]
    public async Task Host_that_was_never_started_is_not_started_by_a_restart()
    {
        var (host, session) = await NewHostAsync(FreePort());
        var newPort = FreePort();
        await session.SetPortAsync(newPort);

        await host.RestartIfPortChangedAsync();

        Assert.Equal(ServerPhase.Starting, host.State.Phase);
        Assert.True(IsPortFree(newPort));
    }

    [Fact]
    public async Task Port_busy_host_starts_after_the_port_is_changed_to_a_free_one()
    {
        var blocker = new TcpListener(IPAddress.Any, 0);
        blocker.Start();
        try
        {
            var busyPort = ((IPEndPoint)blocker.LocalEndpoint).Port;
            var (host, session) = await NewHostAsync(busyPort);
            await host.StartAsync();
            Assert.Equal(ServerPhase.PortBusy, host.State.Phase);
            var newPort = FreePort();
            await session.SetPortAsync(newPort);

            await host.RestartIfPortChangedAsync();

            Assert.Equal((ServerPhase.Running, newPort), (host.State.Phase, host.State.Port));
            Assert.Equal(HttpStatusCode.OK, await StatusCodeAsync(newPort));
        }
        finally
        {
            blocker.Stop();
        }
    }

    [Fact]
    public async Task Port_busy_host_stays_busy_when_the_port_did_not_change()
    {
        var blocker = new TcpListener(IPAddress.Any, 0);
        blocker.Start();
        try
        {
            var busyPort = ((IPEndPoint)blocker.LocalEndpoint).Port;
            var (host, _) = await NewHostAsync(busyPort);
            await host.StartAsync();

            await host.RestartIfPortChangedAsync();

            Assert.Equal((ServerPhase.PortBusy, busyPort), (host.State.Phase, host.State.Port));
        }
        finally
        {
            blocker.Stop();
        }
    }

    [Fact]
    public async Task Failed_host_retries_on_the_new_port_and_reports_the_new_port_in_its_error()
    {
        var (host, session) = await NewHostAsync(FreePort(), IPAddress.Parse("203.0.113.1"));
        await host.StartAsync();
        Assert.Equal(ServerPhase.Failed, host.State.Phase);
        var newPort = FreePort();
        await session.SetPortAsync(newPort);

        await host.RestartIfPortChangedAsync();

        Assert.Equal((ServerPhase.Failed, newPort), (host.State.Phase, host.State.Port));
        Assert.StartsWith($"Error: can't listen on port {newPort} (", host.State.Message);
    }

    [Fact]
    public async Task Concurrent_restarts_after_a_port_change_end_in_one_running_server_on_the_new_port()
    {
        var oldPort = FreePort();
        var (host, session) = await NewHostAsync(oldPort);
        await host.StartAsync();
        var newPort = FreePort();
        await session.SetPortAsync(newPort);

        await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => host.RestartIfPortChangedAsync()));

        Assert.Equal((ServerPhase.Running, newPort), (host.State.Phase, host.State.Port));
        Assert.Equal(HttpStatusCode.OK, await StatusCodeAsync(newPort));
        Assert.True(IsPortFree(oldPort));
    }

    [Fact]
    public async Task Overlapping_restart_and_stop_publish_whole_lifecycles_and_leave_no_listener()
    {
        var (host, session) = await NewHostAsync(FreePort());
        await host.StartAsync();
        var newPort = FreePort();
        await session.SetPortAsync(newPort);
        var phases = new List<ServerPhase>();
        host.Changed += () => phases.Add(host.State.Phase);

        await Task.WhenAll(host.RestartIfPortChangedAsync(), host.StopAsync());

        ServerPhase[] restartThenStop = [ServerPhase.Stopping, ServerPhase.Starting, ServerPhase.Running, ServerPhase.Stopping];
        ServerPhase[] stopOnly = [ServerPhase.Stopping];
        Assert.True(phases.SequenceEqual(restartThenStop) || phases.SequenceEqual(stopOnly), string.Join(",", phases));
        Assert.True(IsPortFree(newPort));
    }

    [Fact]
    public async Task Start_after_stop_serves_again()
    {
        var port = FreePort();
        var (host, _) = await NewHostAsync(port);
        await host.StartAsync();
        await host.StopAsync();

        await host.StartAsync();

        Assert.Equal(HttpStatusCode.OK, await StatusCodeAsync(port));
    }

    [Fact]
    public async Task Restart_after_stop_and_start_follows_the_port_change_again()
    {
        var (host, session) = await NewHostAsync(FreePort());
        await host.StartAsync();
        await host.StopAsync();
        await host.StartAsync();
        var newPort = FreePort();
        await session.SetPortAsync(newPort);

        await host.RestartIfPortChangedAsync();

        Assert.Equal(HttpStatusCode.OK, await StatusCodeAsync(newPort));
    }

    [Fact]
    public async Task Second_start_keeps_the_running_server_without_restarting_it()
    {
        var port = FreePort();
        var (host, _) = await NewHostAsync(port);
        await host.StartAsync();
        var phases = new List<ServerPhase>();
        host.Changed += () => phases.Add(host.State.Phase);

        await host.StartAsync();

        Assert.Empty(phases);
        Assert.Equal(HttpStatusCode.OK, await StatusCodeAsync(port));
    }
}
