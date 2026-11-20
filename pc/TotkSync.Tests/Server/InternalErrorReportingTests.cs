using System.Net;
using System.Text.Json;
using TotkSync.Core.Activity;
using TotkSync.Core.Game;
using TotkSync.Core.Status;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public sealed class InternalErrorReportingTests : IAsyncLifetime
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    private readonly TempDir _temp = new();
    private readonly HttpClient _client = new(new HttpClientHandler { UseProxy = false }) { Timeout = TimeSpan.FromSeconds(20) };
    private readonly List<LanServer> _servers = [];
    private readonly List<Exception> _reported = [];
    private readonly Exception _thrown = new InvalidOperationException("boom in handler");
    private ActivityLog _activity = null!;

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

    private async Task<HttpResponseMessage> RequestStatusWithThrowingHandlerAsync(Action<Exception>? onError)
    {
        var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave: true);
        _activity = new ActivityLog(_temp.Combine("activity.jsonl"));
        var query = new StatusQuery(rig.Cache, new ThrowingGameProcess(_thrown));
        var deps = new ApiDeps(FakeToken, query, _activity, TimeProvider.System, OnError: onError);
        var server = Assert.IsType<ServerStartResult.Running>(await LanServer.StartAsync(IPAddress.Loopback, 0, deps)).Server;
        _servers.Add(server);
        var request = new HttpRequestMessage(HttpMethod.Get, $"http://127.0.0.1:{server.Port}/status");
        request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + FakeToken);
        return await _client.SendAsync(request);
    }

    private async Task<string> JournalTextAsync()
    {
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (_activity.Snapshot().Count < 1 && DateTime.UtcNow < deadline)
        {
            await Task.Delay(20);
        }
        return string.Join("\n", _activity.Snapshot().Select(entry => JsonSerializer.Serialize(entry)));
    }

    private sealed class ThrowingGameProcess(Exception error) : IGameProcess
    {
        public bool IsRunning() => throw error;
    }

    [Fact]
    public async Task Throwing_handler_reports_the_exact_exception_once_and_still_answers_500()
    {
        using var response = await RequestStatusWithThrowingHandlerAsync(_reported.Add);

        Assert.Equal(HttpStatusCode.InternalServerError, response.StatusCode);
        Assert.Same(_thrown, Assert.Single(_reported));
        Assert.Equal("1", response.Headers.GetValues("X-TotkSync-Api").Single());
    }

    [Fact]
    public async Task The_500_body_is_the_contract_internal_error_without_exception_details_or_the_token()
    {
        using var response = await RequestStatusWithThrowingHandlerAsync(_reported.Add);

        var text = await response.Content.ReadAsStringAsync();
        Assert.Equal("""{"error":"internal"}""", text.Replace(" ", "").Replace("\r", "").Replace("\n", ""));
        Assert.DoesNotContain("boom", text);
        Assert.DoesNotContain(FakeToken, text);
    }

    [Fact]
    public async Task The_reported_exception_and_the_journal_entry_never_carry_the_token()
    {
        using var response = await RequestStatusWithThrowingHandlerAsync(_reported.Add);

        var journal = await JournalTextAsync();
        Assert.DoesNotContain(FakeToken, Assert.Single(_reported).ToString());
        Assert.DoesNotContain(FakeToken, journal);
        Assert.Contains("500", journal);
    }

    [Fact]
    public async Task Without_an_error_sink_the_500_is_still_served()
    {
        using var response = await RequestStatusWithThrowingHandlerAsync(null);

        Assert.Equal(HttpStatusCode.InternalServerError, response.StatusCode);
    }

    [Fact]
    public async Task A_sink_that_itself_throws_still_gets_the_marked_contract_500()
    {
        using var response = await RequestStatusWithThrowingHandlerAsync(_ => throw new InvalidOperationException("sink broke"));

        var text = await response.Content.ReadAsStringAsync();
        Assert.Equal(HttpStatusCode.InternalServerError, response.StatusCode);
        Assert.Equal("1", response.Headers.GetValues("X-TotkSync-Api").Single());
        Assert.Equal("""{"error":"internal"}""", text.Replace(" ", "").Replace("\r", "").Replace("\n", ""));
        Assert.DoesNotContain("sink broke", text);
    }

    [Fact]
    public async Task ApiDeps_with_an_error_sink_still_masks_the_token_in_ToString()
    {
        var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave: false);
        var deps = new ApiDeps(FakeToken, rig.Query, new ActivityLog(_temp.Combine("a.jsonl")), TimeProvider.System, OnError: _reported.Add);

        var text = deps.ToString();

        Assert.Contains("Token = ***", text);
        Assert.DoesNotContain(FakeToken, text);
    }
}
