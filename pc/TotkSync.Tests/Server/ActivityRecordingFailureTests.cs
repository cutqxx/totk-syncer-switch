using System.Net;
using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Activity;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class ActivityRecordingFailureTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    private readonly TempDir _temp = new();
    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(20) };
    private LanServer? _server;

    public Task InitializeAsync() => Task.CompletedTask;

    public async Task DisposeAsync()
    {
        _client.Dispose();
        if (_server is not null)
        {
            await _server.DisposeAsync();
        }
        _temp.Dispose();
    }

    private async Task<(LanServer Server, ActivityLog Log)> StartWithThrowingSubscriberAsync()
    {
        var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave: true);
        var log = new ActivityLog(_temp.Combine("activity.jsonl"));
        log.EntryAdded += _ => throw new InvalidOperationException("subscriber failed");
        var time = new FakeTimeProvider(new DateTimeOffset(2026, 11, 7, 10, 0, 0, TimeSpan.Zero));
        var result = await LanServer.StartAsync(IPAddress.Loopback, 0, new ApiDeps(FakeToken, rig.Query, log, time));
        _server = Assert.IsType<ServerStartResult.Running>(result).Server;
        return (_server, log);
    }

    private Task<HttpResponseMessage> GetAsync(LanServer server, string path, string? authorization)
    {
        var request = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{server.Port}{path}");
        if (authorization is not null)
        {
            request.Headers.TryAddWithoutValidation("Authorization", authorization);
        }
        return _client.SendAsync(request);
    }

    [Fact]
    public async Task Successful_request_still_answers_200_when_an_entry_added_subscriber_throws()
    {
        var (server, _) = await StartWithThrowingSubscriberAsync();

        using var response = await GetAsync(server, "/status", "Bearer " + FakeToken);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public async Task Rejected_request_still_answers_401_when_an_entry_added_subscriber_throws()
    {
        var (server, _) = await StartWithThrowingSubscriberAsync();

        using var response = await GetAsync(server, "/status", null);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task Request_is_still_recorded_in_the_log_when_a_subscriber_throws()
    {
        var (server, log) = await StartWithThrowingSubscriberAsync();

        using var response = await GetAsync(server, "/status", "Bearer " + FakeToken);

        var entry = Assert.Single(log.Snapshot());
        Assert.Equal((ActivityKind.StatusOk, 200), (entry.Kind, entry.StatusCode));
    }

    [Fact]
    public async Task Server_keeps_serving_after_a_subscriber_failure()
    {
        var (server, log) = await StartWithThrowingSubscriberAsync();
        using var first = await GetAsync(server, "/status", "Bearer " + FakeToken);

        using var second = await GetAsync(server, "/status", "Bearer " + FakeToken);

        Assert.Equal(HttpStatusCode.OK, second.StatusCode);
        Assert.Equal(2, log.Snapshot().Count);
    }
}
