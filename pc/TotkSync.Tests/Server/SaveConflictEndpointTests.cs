using System.Net;
using System.Text.Json;
using System.Text.Json.Nodes;
using TotkSync.Core.Activity;
using TotkSync.Core.Saves;
using TotkSync.Core.Sync;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class SaveConflictEndpointTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    private static readonly DateTimeOffset PcModifiedInContract = new(2026, 11, 3, 11, 25, 1, TimeSpan.Zero);
    private static readonly DateTimeOffset LastSyncInContract = new(2026, 11, 3, 9, 0, 0, TimeSpan.Zero);

    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(30) };
    private ReceiverRig _rig = null!;
    private ActivityLog _activity = null!;
    private LanServer _server = null!;

    public async Task InitializeAsync()
    {
        _rig = await ReceiverRig.CreateAsync();
        _rig.Time.SetLocalTimeZone(TimeZoneInfo.CreateCustomTimeZone("plus3", TimeSpan.FromHours(3), "plus3", "plus3"));
        _activity = new ActivityLog(_rig.Temp.Combine("activity.jsonl"));
        var deps = new ApiDeps(FakeToken, _rig.Query, _activity, _rig.Time, _rig.Receiver);
        _server = Assert.IsType<ServerStartResult.Running>(await LanServer.StartAsync(IPAddress.Loopback, 0, deps)).Server;
    }

    public async Task DisposeAsync()
    {
        _client.Dispose();
        await _server.DisposeAsync();
        _rig.Dispose();
    }

    private static byte[] DifferentZip => ZipBuilder.ContractZip("valid-save-tree.zip");

    private string[] BackupFiles() => Directory.Exists(_rig.BackupsDir) ? Directory.GetFiles(_rig.BackupsDir) : [];

    private static string ContractText(string name) => File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "contracts", "api", name));

    private static void AssertBodyEqualsContract(string contractFile, string actualJson)
    {
        Assert.True(
            JsonNode.DeepEquals(JsonNode.Parse(ContractText(contractFile)), JsonNode.Parse(actualJson)),
            $"{contractFile} expected {ContractText(contractFile).ReplaceLineEndings("")} but got {actualJson}");
    }

    private async Task<HttpResponseMessage> PutAsync(byte[] body, string path = "/save")
    {
        var request = new HttpRequestMessage(HttpMethod.Put, $"http://127.0.0.1:{_server.Port}{path}") { Content = new ByteArrayContent(body) };
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        return await _client.SendAsync(request);
    }

    private void MakePcSaveModifiedAtContractTime()
    {
        foreach (var file in Directory.EnumerateFiles(_rig.SaveDir, "*", SearchOption.AllDirectories))
        {
            File.SetLastWriteTimeUtc(file, PcModifiedInContract.UtcDateTime);
        }
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

    private async Task<IReadOnlyList<ActivityEntry>> JournalOfAsync(int count)
    {
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (_activity.Snapshot().Count < count && DateTime.UtcNow < deadline)
        {
            await Task.Delay(20);
        }
        return _activity.Snapshot();
    }

    private async Task<string> RoundTripUnchangedAsync()
    {
        using var first = await PutAsync(DifferentZip, "/save?force=1");
        using var second = await PutAsync(DifferentZip);
        return await second.Content.ReadAsStringAsync();
    }

    [Fact]
    public async Task Changed_pc_save_gives_409_matching_the_conflict_contract_with_both_dates()
    {
        MakePcSaveModifiedAtContractTime();
        _rig.SyncState.Save(new LastSync("sha256:" + new string('a', 64), LastSyncInContract, SyncDirection.PcToSwitch));

        using var response = await PutAsync(DifferentZip);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Equal("application/json", response.Content.Headers.ContentType!.MediaType);
        AssertBodyEqualsContract("error-conflict.json", await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task Never_synced_pc_save_gives_409_matching_the_contract_with_null_last_sync()
    {
        MakePcSaveModifiedAtContractTime();

        using var response = await PutAsync(DifferentZip);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        AssertBodyEqualsContract("error-conflict-never-synced.json", await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task Conflict_changes_nothing_and_is_journaled_as_a_conflict()
    {
        var before = SaveInvariants.Capture(_rig);

        using var response = await PutAsync(DifferentZip);

        var entry = await SingleJournalEntryAsync();
        Assert.Equal(("PUT", "/save", 409, ActivityKind.Conflict), (entry.Method, entry.Path, entry.StatusCode, entry.Kind));
        SaveInvariants.AssertSaveIsUntouched(_rig, before);
        Assert.Empty(BackupFiles());
        Assert.False(File.Exists(_rig.Temp.Combine("state", "state.json")));
    }

    [Fact]
    public async Task Force_one_replaces_the_conflicting_save_with_200_after_taking_a_backup()
    {
        var pcSaveBefore = Fingerprint.OfDirectory(_rig.SaveDir);

        using var response = await PutAsync(DifferentZip, "/save?force=1");

        var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync()).RootElement;
        Assert.Equal((HttpStatusCode.OK, "replaced"), (response.StatusCode, body.GetProperty("result").GetString()));
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, Fingerprint.OfDirectory(_rig.SaveDir));
        SaveInvariants.AssertEveryBackupIsACompleteCopyOf(_rig, pcSaveBefore);
        Assert.Single(BackupFiles());
    }

    [Theory]
    [InlineData("/save?force=0")]
    [InlineData("/save?force=true")]
    [InlineData("/save?force=")]
    [InlineData("/save?forced=1")]
    public async Task Anything_but_force_one_is_not_a_force(string path)
    {
        using var response = await PutAsync(DifferentZip, path);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Empty(BackupFiles());
    }

    [Fact]
    public async Task Resending_a_zip_the_pc_already_has_gives_200_unchanged_matching_the_contract()
    {
        var text = await RoundTripUnchangedAsync();

        AssertBodyEqualsContract("put-unchanged.json", text);
    }

    [Fact]
    public async Task Unchanged_creates_no_second_backup_and_is_journaled_as_unchanged()
    {
        await RoundTripUnchangedAsync();

        Assert.Single(BackupFiles());
        var journal = await JournalOfAsync(2);
        Assert.Equal([ActivityKind.SaveReplaced, ActivityKind.SaveUnchanged], journal.Select(entry => entry.Kind).Order());
    }

    [Fact]
    public async Task Unchanged_is_200_even_when_forced_and_creates_no_backup()
    {
        using var identical = await PutAsync(CurrentSaveZip(), "/save?force=1");

        var body = JsonDocument.Parse(await identical.Content.ReadAsStringAsync()).RootElement;
        Assert.Equal((HttpStatusCode.OK, "unchanged"), (identical.StatusCode, body.GetProperty("result").GetString()));
        Assert.Empty(BackupFiles());
    }

    [Fact]
    public async Task State_write_failure_gives_500_write_failed_and_a_retry_gives_unchanged()
    {
        var blocker = _rig.Temp.Combine("state", "state.json.tmp");
        Directory.CreateDirectory(blocker);
        using var failed = await PutAsync(DifferentZip, "/save?force=1");
        Directory.Delete(blocker);

        using var retry = await PutAsync(DifferentZip);

        var failedBody = JsonDocument.Parse(await failed.Content.ReadAsStringAsync()).RootElement;
        Assert.Equal((HttpStatusCode.InternalServerError, "write_failed"), (failed.StatusCode, failedBody.GetProperty("error").GetString()));
        Assert.Contains("sync state was not saved", failedBody.GetProperty("reason").GetString());
        var retryBody = JsonDocument.Parse(await retry.Content.ReadAsStringAsync()).RootElement;
        Assert.Equal((HttpStatusCode.OK, "unchanged"), (retry.StatusCode, retryBody.GetProperty("result").GetString()));
    }

    [Fact]
    public async Task State_write_failure_after_a_replace_is_still_500_but_status_shows_the_new_save_and_no_last_sync()
    {
        Directory.CreateDirectory(_rig.Temp.Combine("state", "state.json.tmp"));

        using var failed = await PutAsync(DifferentZip, "/save?force=1");
        var request = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{_server.Port}/status");
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        using var status = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.InternalServerError, failed.StatusCode);
        var root = JsonDocument.Parse(await status.Content.ReadAsStringAsync()).RootElement;
        Assert.Equal(SaveTreeBuilder.ContractFingerprint, root.GetProperty("save").GetProperty("fingerprint").GetString());
        Assert.Equal(JsonValueKind.Null, root.GetProperty("last_sync").ValueKind);
    }

    [Fact]
    public async Task Status_reports_the_last_sync_after_a_replace()
    {
        using var put = await PutAsync(DifferentZip, "/save?force=1");

        var request = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{_server.Port}/status");
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        using var status = await _client.SendAsync(request);

        var lastSync = JsonDocument.Parse(await status.Content.ReadAsStringAsync()).RootElement.GetProperty("last_sync");
        Assert.Equal(
            (SaveTreeBuilder.ContractFingerprint, "switch->pc"),
            (lastSync.GetProperty("fingerprint").GetString(), lastSync.GetProperty("direction").GetString()));
    }

    private byte[] CurrentSaveZip() => TotkSync.Core.Zip.SaveZipPacker.Pack(_rig.SaveDir);
}
