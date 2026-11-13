using System.Net;
using System.Text.Json;
using TotkSync.Core.Activity;
using TotkSync.Core.Game;
using TotkSync.Core.Sync;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class GameRunningEndpointTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(30) };
    private readonly List<LanServer> _servers = [];
    private ReceiverRig _rig = null!;
    private LanServer _server = null!;

    public async Task InitializeAsync()
    {
        _rig = await ReceiverRig.CreateAsync();
        _server = await StartServerAsync(0);
    }

    public async Task DisposeAsync()
    {
        _client.Dispose();
        foreach (var server in _servers)
        {
            await server.DisposeAsync();
        }
        _rig.Dispose();
    }

    private async Task<LanServer> StartServerAsync(int port)
    {
        var deps = new ApiDeps(FakeToken, _rig.Query, new ActivityLog(_rig.Temp.Combine("activity.jsonl")), _rig.Time, _rig.Receiver);
        var running = Assert.IsType<ServerStartResult.Running>(await LanServer.StartAsync(IPAddress.Loopback, port, deps));
        _servers.Add(running.Server);
        return running.Server;
    }

    private HttpRequestMessage Authorized(HttpMethod method, string path, HttpContent? content = null)
    {
        var request = new HttpRequestMessage(method, $"http://127.0.0.1:{_server.Port}{path}") { Content = content };
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        return request;
    }

    private async Task<HttpStatusCode> PutValidSaveAsync()
    {
        var body = ZipBuilder.ContractZip("valid-save-tree.zip");
        using var response = await _client.SendAsync(Authorized(HttpMethod.Put, "/save?force=1", new ByteArrayContent(body)));
        return response.StatusCode;
    }

    private async Task<string> PutErrorCodeAsync()
    {
        var body = ZipBuilder.ContractZip("valid-save-tree.zip");
        using var response = await _client.SendAsync(Authorized(HttpMethod.Put, "/save?force=1", new ByteArrayContent(body)));
        return JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement.GetProperty("error").GetString()!;
    }

    private async Task<bool> StatusGameRunningAsync()
    {
        using var response = await _client.SendAsync(Authorized(HttpMethod.Get, "/status"));
        return JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement.GetProperty("game_running").GetBoolean();
    }

    [Fact]
    public async Task Put_arriving_while_play_holds_the_lease_gets_503_busy_then_423_game_running_once_the_game_runs()
    {
        var reached = new ManualResetEventSlim();
        var release = new ManualResetEventSlim();
        var launcher = new GameLauncher(_rig.Game, _rig.Gate, _ =>
        {
            reached.Set();
            release.Wait(TimeSpan.FromSeconds(20));
            _rig.Game.Running = true;
        });
        var play = Task.Run(() => launcher.PlayAsync(@"C:\game", CancellationToken.None));
        Assert.True(reached.Wait(TimeSpan.FromSeconds(10)));

        var duringStart = await PutErrorCodeAsync();
        release.Set();
        Assert.IsType<LaunchResult.Started>(await play);
        var afterStart = await PutErrorCodeAsync();

        Assert.Equal(("busy", "game_running"), (duringStart, afterStart));
    }

    [Fact]
    public async Task Status_reports_game_running_while_the_fake_process_runs_and_false_after_exit()
    {
        _rig.Game.Running = true;
        var whileRunning = await StatusGameRunningAsync();
        _rig.Game.Running = false;
        var afterExit = await StatusGameRunningAsync();

        Assert.Equal((true, false), (whileRunning, afterExit));
    }

    [Fact]
    public async Task Game_started_outside_the_launcher_is_not_launched_again_and_put_is_refused_with_423()
    {
        var starts = 0;
        _rig.Game.Running = true;
        var launcher = new GameLauncher(_rig.Game, _rig.Gate, _ => starts++);

        var result = await launcher.PlayAsync(@"C:\game", CancellationToken.None);

        Assert.IsType<LaunchResult.AlreadyRunning>(result);
        Assert.Equal(0, starts);
        Assert.Equal("game_running", await PutErrorCodeAsync());
    }

    [Fact]
    public async Task After_the_game_exits_put_is_served_again()
    {
        _rig.Game.Running = true;
        var refused = await PutErrorCodeAsync();

        _rig.Game.Running = false;
        var status = await PutValidSaveAsync();

        Assert.Equal(("game_running", HttpStatusCode.OK), (refused, status));
    }

    [Fact]
    public async Task Closing_during_a_write_waits_for_it_then_stops_the_server_and_the_port_can_be_reused()
    {
        var port = _server.Port;
        var waitingReported = false;
        var shutdown = new LauncherShutdown(_rig.Gate, _server.StopAsync, () => { });
        _rig.Gate.TryEnter(out var write);

        var closing = shutdown.ShutdownAsync(() => waitingReported = true);
        await Task.Delay(100);
        var gameRunningReportedDuringWrite = await StatusGameRunningAsync();
        write.Dispose();
        await closing;
        var restarted = await StartServerAsync(port);

        Assert.True(waitingReported);
        Assert.False(gameRunningReportedDuringWrite);
        Assert.Equal(port, restarted.Port);
    }
}
