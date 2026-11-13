using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using TotkSync.Core.Activity;
using TotkSync.Core.Saves;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class PutSaveEndpointTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";
    private const string MarkerHeader = "X-TotkSync-Api";
    private const long OverTheLimit = 64L * 1024 * 1024 + 1;

    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(30) };
    private readonly List<LanServer> _servers = [];
    private ReceiverRig _rig = null!;
    private ActivityLog _activity = null!;
    private LanServer _server = null!;

    private string ActivityPath => _rig.Temp.Combine("activity.jsonl");

    public async Task InitializeAsync()
    {
        _rig = await ReceiverRig.CreateAsync();
        _server = await StartServerAsync(withReceiver: true);
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

    private async Task<LanServer> StartServerAsync(bool withReceiver)
    {
        _activity = new ActivityLog(ActivityPath);
        var deps = new ApiDeps(FakeToken, _rig.Query, _activity, _rig.Time, withReceiver ? _rig.Receiver : null);
        var running = Assert.IsType<ServerStartResult.Running>(await LanServer.StartAsync(IPAddress.Loopback, 0, deps));
        _servers.Add(running.Server);
        return running.Server;
    }

    private sealed class TrackedContent(long? declaredLength, byte[] bytes) : HttpContent
    {
        public bool WasSent { get; private set; }

        protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context)
        {
            WasSent = true;
            return stream.WriteAsync(bytes).AsTask();
        }

        protected override bool TryComputeLength(out long length)
        {
            length = declaredLength ?? 0;
            return declaredLength is not null;
        }
    }

    private HttpRequestMessage PutRequest(HttpContent content, string path = "/save?force=1", string? authorization = "Bearer " + FakeToken, bool expectContinue = false)
    {
        var request = new HttpRequestMessage(HttpMethod.Put, $"http://127.0.0.1:{_server.Port}{path}") { Content = content };
        if (authorization is not null)
        {
            request.Headers.TryAddWithoutValidation("Authorization", authorization);
        }
        request.Headers.ExpectContinue = expectContinue;
        return request;
    }

    private Task<HttpResponseMessage> PutAsync(byte[] body, string path = "/save?force=1", string? authorization = "Bearer " + FakeToken)
    {
        return _client.SendAsync(PutRequest(new ByteArrayContent(body), path, authorization));
    }

    private static string ContractText(string name) => File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "contracts", "api", name));

    private static void AssertBodyEqualsContract(string contractFile, string actualJson)
    {
        Assert.True(
            JsonNode.DeepEquals(JsonNode.Parse(ContractText(contractFile)), JsonNode.Parse(actualJson)),
            $"{contractFile} expected {ContractText(contractFile).ReplaceLineEndings("")} but got {actualJson}");
    }

    private async Task<ActivityEntry> SingleJournalEntryAsync()
    {
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (_activity.Snapshot().Count < 1 && DateTime.UtcNow < deadline)
        {
            await Task.Delay(20);
        }
        return Assert.Single(_activity.Snapshot());
    }

    private static byte[] ValidZip => ZipBuilder.ContractZip("valid-save-tree.zip");

    [Fact]
    public async Task Valid_zip_gives_200_with_the_contract_shape_the_new_backup_name_and_fingerprint()
    {
        using var response = await PutAsync(ValidZip);
        var text = await response.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("application/json", response.Content.Headers.ContentType!.MediaType);
        var body = JsonDocument.Parse(text).RootElement;
        var contract = JsonDocument.Parse(ContractText("put-replaced.json")).RootElement;
        Assert.Equal(contract.EnumerateObject().Select(p => p.Name), body.EnumerateObject().Select(p => p.Name));
        Assert.Equal(
            ("replaced", ReceiverRig.ExpectedBackupName, SaveTreeBuilder.ContractFingerprint),
            (body.GetProperty("result").GetString(), body.GetProperty("backup").GetString(), body.GetProperty("fingerprint").GetString()));
    }

    [Fact]
    public async Task Valid_zip_really_replaces_the_save_on_disk_and_status_reports_the_new_fingerprint()
    {
        using var response = await PutAsync(ValidZip);
        using var status = await _client.SendAsync(StatusRequest());

        Assert.Equal(SaveTreeBuilder.ContractFingerprint, Fingerprint.OfDirectory(_rig.SaveDir));
        var json = JsonDocument.Parse(await status.Content.ReadAsStringAsync()).RootElement;
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, json.GetProperty("save").GetProperty("fingerprint").GetString());
    }

    private HttpRequestMessage StatusRequest()
    {
        var request = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{_server.Port}/status");
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        return request;
    }

    [Fact]
    public async Task Success_is_journaled_as_save_replaced_with_method_path_and_status()
    {
        using var response = await PutAsync(ValidZip);

        var entry = await SingleJournalEntryAsync();

        Assert.Equal(("PUT", "/save", 200, ActivityKind.SaveReplaced), (entry.Method, entry.Path, entry.StatusCode, entry.Kind));
    }

    [Fact]
    public async Task Invalid_zip_gives_400_with_the_contract_body_and_changes_nothing()
    {
        var before = SaveInvariants.Capture(_rig);

        using var response = await PutAsync(ZipBuilder.ContractZip("invalid-no-option.zip"));

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        AssertBodyEqualsContract("error-invalid-zip.json", await response.Content.ReadAsStringAsync());
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
        Assert.Equal(ActivityKind.InvalidZip, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Garbage_body_gives_400_with_the_not_a_zip_reason()
    {
        using var response = await PutAsync("this is not a zip"u8.ToArray());

        var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement;
        Assert.Equal((HttpStatusCode.BadRequest, "invalid_zip", "not a zip file"), (response.StatusCode, body.GetProperty("error").GetString(), body.GetProperty("reason").GetString()));
    }

    [Fact]
    public async Task No_save_on_the_pc_gives_404_with_the_contract_body_and_creates_nothing()
    {
        using var rig = await ReceiverRig.CreateAsync(withSave: false);
        var before = TreeSnapshot.Capture(rig.GameDir);
        var server = await StartServerWith(rig);

        using var response = await _client.SendAsync(PutRequestTo(server, ValidZip));

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        AssertBodyEqualsContract("error-no-save.json", await response.Content.ReadAsStringAsync());
        Assert.Equal(before, TreeSnapshot.Capture(rig.GameDir));
        Assert.False(Directory.Exists(rig.BackupsDir));
        Assert.Equal(ActivityKind.NoSave, (await SingleJournalEntryAsync()).Kind);
    }

    private async Task<LanServer> StartServerWith(ReceiverRig rig)
    {
        _activity = new ActivityLog(rig.Temp.Combine("activity-other.jsonl"));
        var deps = new ApiDeps(FakeToken, rig.Query, _activity, rig.Time, rig.Receiver);
        var running = Assert.IsType<ServerStartResult.Running>(await LanServer.StartAsync(IPAddress.Loopback, 0, deps));
        _servers.Add(running.Server);
        return running.Server;
    }

    private static HttpRequestMessage PutRequestTo(LanServer server, byte[] body)
    {
        var request = new HttpRequestMessage(HttpMethod.Put, $"http://127.0.0.1:{server.Port}/save") { Content = new ByteArrayContent(body) };
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        return request;
    }

    [Fact]
    public async Task Running_game_gives_423_with_the_contract_body_and_changes_nothing()
    {
        _rig.Game.Running = true;
        var before = SaveInvariants.Capture(_rig);

        using var response = await PutAsync(ValidZip);

        Assert.Equal(HttpStatusCode.Locked, response.StatusCode);
        AssertBodyEqualsContract("error-game-running.json", await response.Content.ReadAsStringAsync());
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
        Assert.Equal(ActivityKind.GameRunning, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Expect_100_continue_with_the_game_running_gets_423_and_the_body_is_never_sent()
    {
        _rig.Game.Running = true;
        var content = new TrackedContent(ValidZip.Length, ValidZip);

        using var response = await _client.SendAsync(PutRequest(content, expectContinue: true));

        Assert.Equal(HttpStatusCode.Locked, response.StatusCode);
        Assert.False(content.WasSent, "the body must not be uploaded when the PC refuses");
    }

    [Fact]
    public async Task Missing_length_with_chunked_encoding_gives_411_with_the_contract_body_and_no_body_is_sent()
    {
        var content = new TrackedContent(null, ValidZip);
        var before = SaveInvariants.Capture(_rig);

        using var response = await _client.SendAsync(PutRequest(content, expectContinue: true));

        Assert.Equal(HttpStatusCode.LengthRequired, response.StatusCode);
        AssertBodyEqualsContract("error-length-required.json", await response.Content.ReadAsStringAsync());
        Assert.False(content.WasSent);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Equal(ActivityKind.LengthRequired, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Chunked_upload_without_expect_is_also_refused_with_411_and_the_save_is_untouched()
    {
        var before = SaveInvariants.Capture(_rig);

        using var response = await _client.SendAsync(PutRequest(new TrackedContent(null, ValidZip)));

        Assert.Equal(HttpStatusCode.LengthRequired, response.StatusCode);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
    }

    [Fact]
    public async Task Declared_length_over_64_mebibytes_gives_413_with_the_contract_body_and_the_body_is_never_sent()
    {
        var content = new TrackedContent(OverTheLimit, ValidZip);
        var before = SaveInvariants.Capture(_rig);

        using var response = await _client.SendAsync(PutRequest(content, expectContinue: true));

        Assert.Equal(HttpStatusCode.RequestEntityTooLarge, response.StatusCode);
        AssertBodyEqualsContract("error-too-large.json", await response.Content.ReadAsStringAsync());
        Assert.False(content.WasSent);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Equal(ActivityKind.TooLarge, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Backup_failure_gives_500_write_failed_with_a_reason_and_the_save_untouched()
    {
        var before = SaveInvariants.Capture(_rig);
        _rig.Ops.FailOnNth(FileOp.WriteFile, 1);

        using var response = await PutAsync(ValidZip);

        var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement;
        var contract = JsonDocument.Parse(ContractText("error-write-failed.json")).RootElement;
        Assert.Equal(HttpStatusCode.InternalServerError, response.StatusCode);
        Assert.Equal(contract.EnumerateObject().Select(p => p.Name), body.EnumerateObject().Select(p => p.Name));
        Assert.Equal("write_failed", body.GetProperty("error").GetString());
        Assert.StartsWith("backup failed, PC save not changed: ", body.GetProperty("reason").GetString());
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Equal(ActivityKind.WriteFailed, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Replacement_failure_gives_500_write_failed_and_the_save_is_the_old_one()
    {
        var before = SaveInvariants.Capture(_rig);
        _rig.Ops.FailOnNth(FileOp.MoveDirectory, 2);

        using var response = await PutAsync(ValidZip);

        Assert.Equal(HttpStatusCode.InternalServerError, response.StatusCode);
        Assert.Equal("write_failed", JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement.GetProperty("error").GetString());
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.Temp.Path));
    }

    [Fact]
    public async Task Second_put_during_a_write_gets_503_busy_and_status_still_shows_the_old_save_until_the_first_finishes()
    {
        var oldFingerprint = Fingerprint.OfDirectory(_rig.SaveDir);
        var reached = new ManualResetEventSlim();
        var release = new ManualResetEventSlim();
        _rig.Ops.OnBefore = call =>
        {
            if (call.Op == FileOp.WriteFile && call.Path.Contains(".totksync-new-", StringComparison.Ordinal) && !reached.IsSet)
            {
                reached.Set();
                release.Wait(TimeSpan.FromSeconds(20));
            }
        };
        var first = PutAsync(ValidZip);
        Assert.True(reached.Wait(TimeSpan.FromSeconds(10)));

        using var second = await PutAsync(ValidZip);
        using var statusDuring = await _client.SendAsync(StatusRequest());
        release.Set();
        using var firstResponse = await first;

        Assert.Equal(HttpStatusCode.ServiceUnavailable, second.StatusCode);
        AssertBodyEqualsContract("error-busy.json", await second.Content.ReadAsStringAsync());
        var during = JsonDocument.Parse(await statusDuring.Content.ReadAsStringAsync()).RootElement;
        Assert.Equal(oldFingerprint, during.GetProperty("save").GetProperty("fingerprint").GetString());
        Assert.Equal(HttpStatusCode.OK, firstResponse.StatusCode);
        Assert.Contains(_activity.Snapshot(), entry => entry.Kind == ActivityKind.Busy && entry.StatusCode == 503);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("Bearer wrong-token")]
    [InlineData("fake-token-0123456789abcdef")]
    public async Task Put_without_a_valid_token_gets_401_and_the_body_is_never_sent_and_nothing_changes(string? authorization)
    {
        var content = new TrackedContent(ValidZip.Length, ValidZip);
        var before = SaveInvariants.Capture(_rig);

        using var response = await _client.SendAsync(PutRequest(content, authorization: authorization, expectContinue: true));

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
        AssertBodyEqualsContract("error-unauthorized.json", await response.Content.ReadAsStringAsync());
        Assert.False(content.WasSent);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
        Assert.Equal(ActivityKind.Unauthorized, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Put_with_a_wrong_token_and_a_body_sent_anyway_still_changes_nothing()
    {
        var before = SaveInvariants.Capture(_rig);

        using var response = await PutAsync(ValidZip, authorization: "Bearer wrong");

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
    }

    [Theory]
    [InlineData("GET")]
    [InlineData("POST")]
    [InlineData("DELETE")]
    [InlineData("PATCH")]
    public async Task Other_methods_on_save_get_405_with_allow_put_and_do_not_touch_the_save(string method)
    {
        var before = SaveInvariants.Capture(_rig);
        var request = new HttpRequestMessage(new HttpMethod(method), $"http://127.0.0.1:{_server.Port}/save");
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);

        using var response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.MethodNotAllowed, response.StatusCode);
        Assert.Equal(["PUT"], response.Content.Headers.Allow);
        AssertBodyEqualsContract("error-method-not-allowed.json", await response.Content.ReadAsStringAsync());
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Equal(ActivityKind.MethodNotAllowed, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Get_on_save_without_a_token_is_401_not_405()
    {
        var request = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{_server.Port}/save");

        using var response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task Save_route_does_not_exist_when_no_receiver_is_wired()
    {
        var server = await StartServerAsync(withReceiver: false);
        var request = PutRequestTo(server, ValidZip);

        using var response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        Assert.Equal("not_found", JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement.GetProperty("error").GetString());
    }

    [Fact]
    public async Task Every_kind_of_put_outcome_carries_the_api_marker_header()
    {
        _rig.Game.Running = true;
        var running = await PutAsync(ValidZip);
        _rig.Game.Running = false;
        var invalid = await PutAsync("junk"u8.ToArray());
        var unauthorized = await PutAsync(ValidZip, authorization: null);
        var tooLarge = await _client.SendAsync(PutRequest(new TrackedContent(OverTheLimit, []), expectContinue: true));
        var replaced = await PutAsync(ValidZip);

        Assert.All(new[] { running, invalid, unauthorized, tooLarge, replaced }, response => Assert.Equal(["1"], response.Headers.GetValues(MarkerHeader)));
    }

    [Fact]
    public async Task Upload_cut_off_halfway_is_journaled_as_write_failed_and_nothing_is_written()
    {
        var before = SaveInvariants.Capture(_rig);
        using (var tcp = new TcpClient())
        {
            await tcp.ConnectAsync(IPAddress.Loopback, _server.Port);
            var stream = tcp.GetStream();
            var header = $"PUT /save HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer {FakeToken}\r\nContent-Length: 100000\r\n\r\n";
            await stream.WriteAsync(Encoding.ASCII.GetBytes(header));
            await stream.WriteAsync(ValidZip.AsMemory(0, 100));
            await stream.FlushAsync();
        }

        var entry = await SingleJournalEntryAsync();

        Assert.Equal(ActivityKind.WriteFailed, entry.Kind);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.False(Directory.Exists(_rig.BackupsDir));
        Assert.False(_rig.Gate.IsBusy);
    }

    [Fact]
    public async Task The_token_appears_in_no_response_body_header_or_journal_line()
    {
        var responses = new List<HttpResponseMessage>();
        responses.Add(await PutAsync(ValidZip));
        responses.Add(await PutAsync("junk"u8.ToArray()));
        responses.Add(await PutAsync(ValidZip, authorization: "Bearer wrong"));
        _rig.Game.Running = true;
        responses.Add(await PutAsync(ValidZip));
        await SingleOrMoreJournalEntriesAsync(4);

        var everything = new StringBuilder();
        foreach (var response in responses)
        {
            everything.Append(response.Headers).Append(response.Content.Headers).Append(await response.Content.ReadAsStringAsync());
        }
        everything.Append(File.ReadAllText(ActivityPath));
        everything.Append(string.Join('\n', _activity.Snapshot()));

        Assert.DoesNotContain(FakeToken, everything.ToString());
    }

    private async Task SingleOrMoreJournalEntriesAsync(int count)
    {
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (_activity.Snapshot().Count < count && DateTime.UtcNow < deadline)
        {
            await Task.Delay(20);
        }
    }
}
