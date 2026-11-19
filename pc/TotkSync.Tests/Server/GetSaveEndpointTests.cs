using System.IO.Compression;
using System.Net;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using TotkSync.Core.Activity;
using TotkSync.Core.Saves;
using TotkSync.Core.Sync;
using TotkSync.Core.Zip;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class GetSaveEndpointTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";
    private const string OtherFingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000";

    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(30) };
    private readonly List<LanServer> _servers = [];
    private ReceiverRig _rig = null!;
    private ActivityLog _activity = null!;
    private LanServer _server = null!;

    public async Task InitializeAsync()
    {
        _rig = await ReceiverRig.CreateAsync();
        _server = await StartServerAsync(_rig);
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

    private string StatePath => _rig.Temp.Combine("state", "state.json");

    private async Task<LanServer> StartServerAsync(ReceiverRig rig)
    {
        _activity = new ActivityLog(rig.Temp.Combine("activity.jsonl"));
        var deps = new ApiDeps(FakeToken, rig.Query, _activity, rig.Time, rig.Receiver, Sender: rig.Sender);
        var running = Assert.IsType<ServerStartResult.Running>(await LanServer.StartAsync(IPAddress.Loopback, 0, deps));
        _servers.Add(running.Server);
        return running.Server;
    }

    private Task<HttpResponseMessage> SendAsync(HttpMethod method, string path, HttpContent? content = null, LanServer? server = null)
    {
        var request = new HttpRequestMessage(method, $"http://127.0.0.1:{(server ?? _server).Port}{path}") { Content = content };
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        return _client.SendAsync(request);
    }

    private Task<HttpResponseMessage> GetSaveAsync(LanServer? server = null) => SendAsync(HttpMethod.Get, "/save", server: server);

    private Task<HttpResponseMessage> AckAsync(string body)
    {
        return SendAsync(HttpMethod.Post, "/save/ack", new StringContent(body, Encoding.UTF8, "application/json"));
    }

    private static string AckBody(string fingerprint) => JsonSerializer.Serialize(new { fingerprint });

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

    private async Task<JsonElement> StatusAsync()
    {
        using var response = await SendAsync(HttpMethod.Get, "/status");
        return JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement;
    }

    private static string Header(HttpResponseMessage response, string name) => response.Headers.GetValues(name).Single();

    [Fact]
    public async Task Get_save_answers_a_zip_with_the_fingerprint_and_modified_headers_matching_status()
    {
        using var response = await GetSaveAsync();
        var zip = await response.Content.ReadAsByteArrayAsync();
        var status = await StatusAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("application/zip", response.Content.Headers.ContentType!.MediaType);
        Assert.Equal(zip.Length, response.Content.Headers.ContentLength);
        Assert.Equal(Fingerprint.OfDirectory(_rig.SaveDir), Header(response, "X-TotkSync-Fingerprint"));
        Assert.Equal(status.GetProperty("save").GetProperty("modified").GetString(), Header(response, "X-TotkSync-Modified"));
        Assert.Equal("1", Header(response, "X-TotkSync-Api"));
    }

    [Fact]
    public async Task The_zip_is_a_valid_save_zip_whose_files_match_the_pc_save_byte_for_byte()
    {
        using var response = await GetSaveAsync();
        var zip = await response.Content.ReadAsByteArrayAsync();

        var valid = Assert.IsType<ZipValidation.Valid>(SaveZipValidator.Validate(zip)).Zip;
        var onDisk = SaveFile.Enumerate(_rig.SaveDir).ToDictionary(file => file.RelPath, file => File.ReadAllBytes(file.FullPath));
        Assert.Equal(onDisk.Keys.Order(StringComparer.Ordinal), valid.Files.Select(file => file.RelPath).Order(StringComparer.Ordinal));
        Assert.All(valid.Files, file => Assert.Equal(onDisk[file.RelPath], file.Content));
        Assert.Contains("old-empty-dir", valid.Directories);
        Assert.Equal(Header(response, "X-TotkSync-Fingerprint"), Fingerprint.OfZip(valid));
    }

    [Fact]
    public async Task The_zip_has_no_junk_and_no_jksv_meta_and_everything_sits_at_the_root()
    {
        SaveTreeBuilder.WriteFile(_rig.SaveDir, ".nx_save_meta.bin", "JKSV-meta");
        using var response = await GetSaveAsync();

        using var archive = new ZipArchive(new MemoryStream(await response.Content.ReadAsByteArrayAsync()), ZipArchiveMode.Read);
        var names = archive.Entries.Select(entry => entry.FullName).ToList();
        Assert.Contains("option.sav", names);
        Assert.Contains("slot_00/progress.sav", names);
        Assert.DoesNotContain(names, name => JunkFilter.IsJunk(name.TrimEnd('/')));
        Assert.DoesNotContain(names, name => name.StartsWith('/') || name.Contains('\\'));
    }

    [Fact]
    public async Task Get_save_changes_neither_the_save_nor_the_sync_state()
    {
        _rig.SyncState.Save(new LastSync(OtherFingerprint, ReceiverRig.FakeNow, SyncDirection.SwitchToPc));
        var stateBefore = File.ReadAllBytes(StatePath);
        var before = SaveInvariants.Capture(_rig);

        using var response = await GetSaveAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Equal(stateBefore, File.ReadAllBytes(StatePath));
        Assert.False(Directory.Exists(_rig.BackupsDir));
    }

    [Fact]
    public async Task Get_save_is_journaled_as_save_sent()
    {
        using var response = await GetSaveAsync();

        var entry = await SingleJournalEntryAsync();
        Assert.Equal(("GET", "/save", 200, ActivityKind.SaveSent), (entry.Method, entry.Path, entry.StatusCode, entry.Kind));
    }

    [Fact]
    public async Task While_totk_runs_get_save_is_refused_with_423_and_no_zip()
    {
        _rig.Game.Running = true;

        using var response = await GetSaveAsync();

        Assert.Equal(HttpStatusCode.Locked, response.StatusCode);
        AssertBodyEqualsContract("error-game-running.json", await response.Content.ReadAsStringAsync());
        Assert.Equal(ActivityKind.GameRunning, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Without_a_pc_save_get_save_is_404_no_save()
    {
        using var rig = await ReceiverRig.CreateAsync(withSave: false);
        var server = await StartServerAsync(rig);

        using var response = await GetSaveAsync(server);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        AssertBodyEqualsContract("error-no-save.json", await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task While_another_transfer_holds_the_gate_get_save_is_503_busy()
    {
        Assert.True(_rig.Gate.TryEnter(out var lease));
        using (lease)
        {
            using var response = await GetSaveAsync();

            Assert.Equal(HttpStatusCode.ServiceUnavailable, response.StatusCode);
            AssertBodyEqualsContract("error-busy.json", await response.Content.ReadAsStringAsync());
        }
    }

    [Fact]
    public async Task Ack_with_the_current_fingerprint_records_a_pc_to_switch_sync()
    {
        var fingerprint = Fingerprint.OfDirectory(_rig.SaveDir);

        using var response = await AckAsync(AckBody(fingerprint));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        AssertBodyEqualsContract("ack-recorded.json", await response.Content.ReadAsStringAsync());
        Assert.Equal(new LastSync(fingerprint, ReceiverRig.FakeNow, SyncDirection.PcToSwitch), _rig.SyncState.Load());
        var lastSync = (await StatusAsync()).GetProperty("last_sync");
        Assert.Equal(("pc->switch", fingerprint), (lastSync.GetProperty("direction").GetString(), lastSync.GetProperty("fingerprint").GetString()));
    }

    [Fact]
    public async Task Ack_is_journaled_as_sync_confirmed()
    {
        using var response = await AckAsync(AckBody(Fingerprint.OfDirectory(_rig.SaveDir)));

        var entry = await SingleJournalEntryAsync();
        Assert.Equal(("POST", "/save/ack", 200, ActivityKind.SyncConfirmed), (entry.Method, entry.Path, entry.StatusCode, entry.Kind));
    }

    [Fact]
    public async Task Ack_with_another_fingerprint_is_409_and_leaves_the_sync_state_alone()
    {
        _rig.SyncState.Save(new LastSync(OtherFingerprint, ReceiverRig.FakeNow, SyncDirection.SwitchToPc));
        var stateBefore = File.ReadAllBytes(StatePath);

        using var response = await AckAsync(AckBody("sha256:" + new string('a', 64)));

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        AssertBodyEqualsContract("error-fingerprint-mismatch.json", await response.Content.ReadAsStringAsync());
        Assert.Equal(stateBefore, File.ReadAllBytes(StatePath));
        Assert.Equal(ActivityKind.FingerprintMismatch, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Ack_without_a_sync_state_and_a_wrong_fingerprint_creates_no_state_file()
    {
        using var response = await AckAsync(AckBody(OtherFingerprint));

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.False(File.Exists(StatePath));
    }

    [Theory]
    [InlineData("not json")]
    [InlineData("{}")]
    [InlineData("""{"fingerprint":null}""")]
    [InlineData("""{"fingerprint":"abc"}""")]
    [InlineData("""{"fingerprint":"sha256:ABCDEF0000000000000000000000000000000000000000000000000000000000"}""")]
    [InlineData("""{"fingerprint":42}""")]
    public async Task Malformed_ack_is_400_bad_request_and_records_nothing(string body)
    {
        using var response = await AckAsync(body);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        AssertBodyEqualsContract("error-bad-request.json", await response.Content.ReadAsStringAsync());
        Assert.False(File.Exists(StatePath));
        Assert.Equal(ActivityKind.BadRequest, (await SingleJournalEntryAsync()).Kind);
    }

    [Fact]
    public async Task Oversized_ack_is_400_without_reading_it()
    {
        using var response = await AckAsync(AckBody(Fingerprint.OfDirectory(_rig.SaveDir)) + new string(' ', 2000));

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.False(File.Exists(StatePath));
    }

    [Fact]
    public async Task While_totk_runs_ack_is_423_and_records_nothing()
    {
        var fingerprint = Fingerprint.OfDirectory(_rig.SaveDir);
        _rig.Game.Running = true;

        using var response = await AckAsync(AckBody(fingerprint));

        Assert.Equal(HttpStatusCode.Locked, response.StatusCode);
        Assert.False(File.Exists(StatePath));
    }

    [Fact]
    public async Task Save_route_allows_get_and_put_and_ack_route_allows_only_post()
    {
        using var onSave = await SendAsync(HttpMethod.Delete, "/save");
        using var onAck = await SendAsync(HttpMethod.Get, "/save/ack");

        Assert.Equal(HttpStatusCode.MethodNotAllowed, onSave.StatusCode);
        Assert.Equal(["GET", "PUT"], onSave.Content.Headers.Allow);
        Assert.Equal(HttpStatusCode.MethodNotAllowed, onAck.StatusCode);
        Assert.Equal(["POST"], onAck.Content.Headers.Allow);
    }

    [Fact]
    public async Task Get_save_and_ack_without_a_token_are_401()
    {
        var get = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{_server.Port}/save");
        var ack = new HttpRequestMessage(HttpMethod.Post, $"http://127.0.0.1:{_server.Port}/save/ack") { Content = new StringContent(AckBody(OtherFingerprint)) };

        using var getResponse = await _client.SendAsync(get);
        using var ackResponse = await _client.SendAsync(ack);

        Assert.Equal(HttpStatusCode.Unauthorized, getResponse.StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, ackResponse.StatusCode);
        Assert.False(File.Exists(StatePath));
    }
}
