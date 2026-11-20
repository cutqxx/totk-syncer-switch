using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Activity;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class LanServerTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";
    private const string MarkerHeader = "X-TotkSync-Api";

    private readonly TempDir _temp = new();
    private readonly List<LanServer> _servers = [];
    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(20) };
    private ActivityLog _activity = null!;

    private string ActivityPath => _temp.Combine("activity.jsonl");

    public Task InitializeAsync() => Task.CompletedTask;

    public async Task DisposeAsync()
    {
        _client.Dispose();
        foreach (var server in _servers)
        {
            await server.DisposeAsync();
        }
        _temp.Dispose();
    }

    private async Task<(LanServer Server, StatusRig Rig)> StartAsync(bool withSave = true, string? lastSyncJson = null, string token = FakeToken)
    {
        var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave, lastSyncJson);
        _activity = new ActivityLog(ActivityPath);
        var time = new FakeTimeProvider(new DateTimeOffset(2026, 11, 7, 10, 0, 0, TimeSpan.Zero));
        var deps = new ApiDeps(token, rig.Query, _activity, time);
        var result = await LanServer.StartAsync(IPAddress.Loopback, 0, deps);
        var running = Assert.IsType<ServerStartResult.Running>(result);
        _servers.Add(running.Server);
        return (running.Server, rig);
    }

    private Task<HttpResponseMessage> SendAsync(LanServer server, string path, HttpMethod? method = null, string? authorization = "Bearer " + FakeToken)
    {
        var request = new HttpRequestMessage(method ?? HttpMethod.Get, $"http://127.0.0.1:{server.Port}{path}");
        if (authorization is not null)
        {
            request.Headers.TryAddWithoutValidation("Authorization", authorization);
        }
        return _client.SendAsync(request);
    }

    private static string ContractText(string name)
    {
        return File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "contracts", "api", name));
    }

    private static async Task<JsonElement> JsonBodyAsync(HttpResponseMessage response)
    {
        return JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement.Clone();
    }

    private static string Shape(JsonElement element)
    {
        return element.ValueKind switch
        {
            JsonValueKind.Object => "{" + string.Join(",", element.EnumerateObject().Select(p => $"{p.Name}:{Shape(p.Value)}")) + "}",
            JsonValueKind.True or JsonValueKind.False => "bool",
            var kind => kind.ToString(),
        };
    }

    private static string ContractShape(string name) => Shape(JsonDocument.Parse(ContractText(name)).RootElement);

    private async Task<IReadOnlyList<ActivityEntry>> WaitForJournalAsync(int count)
    {
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (_activity.Snapshot().Count < count && DateTime.UtcNow < deadline)
        {
            await Task.Delay(20);
        }
        return _activity.Snapshot();
    }

    [Fact]
    public async Task Status_with_valid_token_returns_200_json_matching_the_ok_contract()
    {
        var (server, _) = await StartAsync(lastSyncJson: StatusRig.LastSyncJson("switch->pc"));

        var response = await SendAsync(server, "/status");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("application/json", response.Content.Headers.ContentType!.MediaType);
        var body = await JsonBodyAsync(response);
        Assert.Equal(ContractShape("status-ok.json"), Shape(body));
        Assert.Equal(("totk-sync", 1, false, "switch->pc"),
            (body.GetProperty("app").GetString(), body.GetProperty("api").GetInt32(),
             body.GetProperty("game_running").GetBoolean(), body.GetProperty("last_sync").GetProperty("direction").GetString()));
    }

    [Fact]
    public async Task Status_while_game_runs_matches_the_game_running_contract()
    {
        var (server, rig) = await StartAsync(lastSyncJson: StatusRig.LastSyncJson("pc->switch"));
        rig.Game.Running = true;

        var body = await JsonBodyAsync(await SendAsync(server, "/status"));

        Assert.Equal(ContractShape("status-game-running.json"), Shape(body));
        Assert.True(body.GetProperty("game_running").GetBoolean());
        Assert.Equal("pc->switch", body.GetProperty("last_sync").GetProperty("direction").GetString());
    }

    [Fact]
    public async Task Status_of_a_save_that_was_never_synced_has_null_last_sync()
    {
        var (server, _) = await StartAsync();

        var body = await JsonBodyAsync(await SendAsync(server, "/status"));

        Assert.Equal(ContractShape("status-never-synced.json"), Shape(body));
    }

    [Fact]
    public async Task Status_without_a_save_says_not_present_and_leaks_no_save_details()
    {
        var (server, _) = await StartAsync(withSave: false);

        var response = await SendAsync(server, "/status");
        var text = await response.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal(ContractShape("status-no-save.json"), Shape(JsonDocument.Parse(text).RootElement));
        Assert.DoesNotContain("fingerprint", text);
    }

    [Fact]
    public async Task Status_body_reports_live_game_state_on_each_request()
    {
        var (server, rig) = await StartAsync();

        var first = (await JsonBodyAsync(await SendAsync(server, "/status"))).GetProperty("game_running").GetBoolean();
        rig.Game.Running = true;
        var second = (await JsonBodyAsync(await SendAsync(server, "/status"))).GetProperty("game_running").GetBoolean();

        Assert.Equal([false, true], [first, second]);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("Bearer ")]
    [InlineData("Bearer wrong-token")]
    [InlineData("Bearer fake-token-0123456789abcde")]
    [InlineData("fake-token-0123456789abcdef")]
    public async Task Status_without_or_with_wrong_token_is_401_with_the_contract_body_and_no_save_data(string? authorization)
    {
        var (server, _) = await StartAsync();

        var response = await SendAsync(server, "/status", authorization: authorization);
        var text = await response.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
        Assert.True(JsonNode.DeepEquals(JsonNode.Parse(ContractText("error-unauthorized.json")), JsonNode.Parse(text)));
        Assert.DoesNotContain("save", text);
    }

    [Fact]
    public async Task Token_in_the_query_string_is_not_accepted()
    {
        var (server, _) = await StartAsync();

        var response = await SendAsync(server, "/status?token=" + FakeToken, authorization: null);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task Empty_token_in_settings_does_not_let_an_empty_bearer_through()
    {
        var (server, _) = await StartAsync(token: "");

        var withEmpty = await SendAsync(server, "/status", authorization: "Bearer ");
        var withNone = await SendAsync(server, "/status", authorization: null);

        Assert.Equal([HttpStatusCode.Unauthorized, HttpStatusCode.Unauthorized], [withEmpty.StatusCode, withNone.StatusCode]);
    }

    [Fact]
    public async Task Unknown_path_with_token_is_404_with_the_contract_body()
    {
        var (server, _) = await StartAsync();

        var response = await SendAsync(server, "/nope");

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        Assert.Equal("not_found", (await JsonBodyAsync(response)).GetProperty("error").GetString());
        Assert.Equal(ContractShape("error-not-found.json"), Shape(await JsonBodyAsync(await SendAsync(server, "/nope"))));
    }

    [Fact]
    public async Task Unknown_path_without_token_is_401_so_routes_are_not_revealed()
    {
        var (server, _) = await StartAsync();

        var response = await SendAsync(server, "/nope", authorization: null);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Theory]
    [InlineData("POST")]
    [InlineData("PUT")]
    [InlineData("DELETE")]
    [InlineData("PATCH")]
    public async Task Other_methods_on_status_are_405_with_allow_get(string method)
    {
        var (server, _) = await StartAsync();

        var response = await SendAsync(server, "/status", new HttpMethod(method));

        Assert.Equal(HttpStatusCode.MethodNotAllowed, response.StatusCode);
        Assert.Equal(["GET"], response.Content.Headers.Allow);
        Assert.Equal("method_not_allowed", (await JsonBodyAsync(response)).GetProperty("error").GetString());
    }

    [Fact]
    public async Task Method_error_body_matches_the_contract_shape()
    {
        var (server, _) = await StartAsync();

        var response = await SendAsync(server, "/status", HttpMethod.Post);

        Assert.Equal(ContractShape("error-method-not-allowed.json"), Shape(await JsonBodyAsync(response)));
    }

    [Fact]
    public async Task Post_to_status_without_token_is_401_not_405()
    {
        var (server, _) = await StartAsync();

        var response = await SendAsync(server, "/status", HttpMethod.Post, authorization: null);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task Rejected_requests_do_not_change_the_save_on_disk()
    {
        var (server, rig) = await StartAsync();
        var before = TotkSync.Core.Saves.SaveSnapshot.Capture(rig.SaveDir).Fingerprint;

        await SendAsync(server, "/status", HttpMethod.Post);
        await SendAsync(server, "/status", HttpMethod.Delete);
        await SendAsync(server, "/nope", HttpMethod.Put);

        Assert.Equal(before, TotkSync.Core.Saves.SaveSnapshot.Capture(rig.SaveDir).Fingerprint);
    }

    [Fact]
    public async Task Every_response_kind_carries_the_api_marker_and_no_server_header()
    {
        var (server, _) = await StartAsync();

        var responses = new[]
        {
            await SendAsync(server, "/status"),
            await SendAsync(server, "/status", authorization: null),
            await SendAsync(server, "/nope"),
            await SendAsync(server, "/nope", authorization: null),
            await SendAsync(server, "/status", HttpMethod.Post),
        };

        Assert.All(responses, response =>
        {
            Assert.Equal(["1"], response.Headers.GetValues(MarkerHeader));
            Assert.False(response.Headers.Contains("Server"));
        });
    }

    [Fact]
    public async Task Each_request_adds_exactly_one_journal_entry_with_client_method_path_and_result()
    {
        var (server, _) = await StartAsync();

        await SendAsync(server, "/status");
        await SendAsync(server, "/status", authorization: null);
        await SendAsync(server, "/nope");
        await SendAsync(server, "/status", HttpMethod.Post);
        var entries = await WaitForJournalAsync(4);
        await Task.Delay(100);

        Assert.Equal(
            [
                ("127.0.0.1", "GET", "/status", 200, ActivityKind.StatusOk),
                ("127.0.0.1", "GET", "/status", 401, ActivityKind.Unauthorized),
                ("127.0.0.1", "GET", "/nope", 404, ActivityKind.NotFound),
                ("127.0.0.1", "POST", "/status", 405, ActivityKind.MethodNotAllowed),
            ],
            _activity.Snapshot().Select(e => (e.Client, e.Method, e.Path, e.StatusCode, e.Kind)));
        Assert.Equal(new DateTimeOffset(2026, 11, 7, 10, 0, 0, TimeSpan.Zero), entries[0].Time);
    }

    [Fact]
    public async Task Journal_file_never_contains_the_token_or_query_string()
    {
        var (server, _) = await StartAsync();

        await SendAsync(server, "/status?secret=QUERYSECRET", authorization: "Bearer " + FakeToken);
        await SendAsync(server, "/status", authorization: "Bearer wrong-token-value");
        await WaitForJournalAsync(2);
        await Task.Delay(100);

        var text = File.ReadAllText(ActivityPath);
        Assert.DoesNotContain(FakeToken, text);
        Assert.DoesNotContain("wrong-token-value", text);
        Assert.DoesNotContain("QUERYSECRET", text);
        Assert.Equal("/status", _activity.Snapshot()[0].Path);
    }

    [Fact]
    public async Task Overlong_path_and_method_are_truncated_in_the_journal()
    {
        var (server, _) = await StartAsync();

        await SendAsync(server, "/" + new string('a', 400));
        await SendAsync(server, "/x", new HttpMethod(new string('M', 40)));
        var entries = await WaitForJournalAsync(2);

        Assert.Equal((200, 16), (entries[0].Path.Length, entries[1].Method.Length));
    }

    [Fact]
    public async Task Port_held_on_all_interfaces_is_reported_as_port_busy()
    {
        var blocker = new TcpListener(IPAddress.Any, 0);
        blocker.Start();
        try
        {
            var port = ((IPEndPoint)blocker.LocalEndpoint).Port;
            var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave: false);
            var deps = new ApiDeps(FakeToken, rig.Query, new ActivityLog(ActivityPath), TimeProvider.System);

            var result = await LanServer.StartAsync(IPAddress.Any, port, deps);

            Assert.IsType<ServerStartResult.PortBusy>(result);
        }
        finally
        {
            blocker.Stop();
        }
    }

    [Fact]
    public async Task Port_becomes_usable_again_after_the_server_is_disposed()
    {
        var (server, rig) = await StartAsync();
        var port = server.Port;
        await server.DisposeAsync();
        _servers.Remove(server);
        var deps = new ApiDeps(FakeToken, rig.Query, new ActivityLog(ActivityPath), TimeProvider.System);

        var restarted = await LanServer.StartAsync(IPAddress.Loopback, port, deps);

        var running = Assert.IsType<ServerStartResult.Running>(restarted);
        _servers.Add(running.Server);
        Assert.Equal(port, running.Server.Port);
    }

    [Fact]
    public async Task Server_stops_answering_after_stop()
    {
        var (server, _) = await StartAsync();
        await server.StopAsync();

        await Assert.ThrowsAsync<HttpRequestException>(() => SendAsync(server, "/status"));
    }

    [Theory]
    [InlineData("/favicon.ico")]
    [InlineData("/save.json")]
    public async Task File_like_unknown_paths_get_the_json_404_contract(string path)
    {
        var (server, _) = await StartAsync();

        var response = await SendAsync(server, path);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        Assert.Equal(ContractShape("error-not-found.json"), Shape(await JsonBodyAsync(response)));
        Assert.Equal("1", response.Headers.GetValues(MarkerHeader).Single());
    }

    [Fact]
    public async Task Exception_in_a_handler_gives_500_contract_body_marker_and_a_journal_entry()
    {
        var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave: true);
        _activity = new ActivityLog(ActivityPath);
        var query = new TotkSync.Core.Status.StatusQuery(rig.Cache, new ThrowingGameProcess());
        var result = await LanServer.StartAsync(IPAddress.Loopback, 0, new ApiDeps(FakeToken, query, _activity, TimeProvider.System));
        var server = Assert.IsType<ServerStartResult.Running>(result).Server;
        _servers.Add(server);

        var response = await SendAsync(server, "/status");

        Assert.Equal(HttpStatusCode.InternalServerError, response.StatusCode);
        var text = await response.Content.ReadAsStringAsync();
        Assert.Equal(ContractShape("error-internal.json"), Shape(JsonDocument.Parse(text).RootElement));
        Assert.DoesNotContain(FakeToken, text);
        Assert.DoesNotContain("boom", text);
        Assert.Equal("1", response.Headers.GetValues(MarkerHeader).Single());
        var entry = Assert.Single(await WaitForJournalAsync(1));
        Assert.Equal((ActivityKind.Internal, 500), (entry.Kind, entry.StatusCode));
    }

    private sealed class ThrowingGameProcess : TotkSync.Core.Game.IGameProcess
    {
        public bool IsRunning() => throw new InvalidOperationException("boom");
    }
}
