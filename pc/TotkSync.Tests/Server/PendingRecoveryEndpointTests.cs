using System.Net;
using System.Text.Json;
using TotkSync.Core.Activity;
using TotkSync.Core.Replace;
using TotkSync.Core.Sync;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class PendingRecoveryEndpointTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(30) };
    private readonly List<bool> _backupsExistWhenRecoveryRan = [];
    private ReceiverRig _rig = null!;
    private PendingRecovery _recovery = null!;
    private LanServer _server = null!;

    public async Task InitializeAsync()
    {
        _rig = await ReceiverRig.CreateAsync();
        _recovery = new PendingRecovery(_rig.Game, _rig.Gate, () =>
        {
            _backupsExistWhenRecoveryRan.Add(Directory.Exists(_rig.BackupsDir));
            return [];
        }, _ => { });
        var receiver = new SaveReceiver(_rig.Settings, _rig.Game, _rig.Gate, _rig.Cache, _rig.Backups, _rig.Replacer, _rig.SyncState, _rig.Time, _recovery);
        var deps = new ApiDeps(FakeToken, _rig.Query, new ActivityLog(_rig.Temp.Combine("activity.jsonl")), _rig.Time, receiver);
        _server = Assert.IsType<ServerStartResult.Running>(await LanServer.StartAsync(IPAddress.Loopback, 0, deps)).Server;
    }

    public async Task DisposeAsync()
    {
        _client.Dispose();
        await _server.DisposeAsync();
        _rig.Dispose();
    }

    private async Task<(HttpStatusCode Status, string Error)> PutValidSaveAsync()
    {
        var request = new HttpRequestMessage(HttpMethod.Put, $"http://127.0.0.1:{_server.Port}/save?force=1")
        {
            Content = new ByteArrayContent(ZipBuilder.ContractZip("valid-save-tree.zip")),
        };
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        using var response = await _client.SendAsync(request);
        var text = await response.Content.ReadAsStringAsync();
        var error = response.IsSuccessStatusCode ? "" : JsonDocument.Parse(text).RootElement.GetProperty("error").GetString()!;
        return (response.StatusCode, error);
    }

    [Fact]
    public async Task Put_with_recovery_pending_runs_it_once_before_anything_is_written_and_then_succeeds()
    {
        var (status, _) = await PutValidSaveAsync();
        await PutValidSaveAsync();

        Assert.Equal(HttpStatusCode.OK, status);
        Assert.Equal([false], _backupsExistWhenRecoveryRan);
        Assert.False(_recovery.IsPending);
    }

    [Fact]
    public async Task Put_with_the_game_running_is_refused_and_leaves_recovery_pending()
    {
        _rig.Game.Running = true;

        var (status, error) = await PutValidSaveAsync();

        Assert.Equal((HttpStatusCode.Locked, "game_running"), (status, error));
        Assert.Empty(_backupsExistWhenRecoveryRan);
        Assert.True(_recovery.IsPending);
    }
}
