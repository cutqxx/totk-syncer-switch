using System.Net;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using TotkSync.Core.Activity;
using TotkSync.Server;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Server;

public class PipelineGuardTests : IDisposable
{
    private const string FakeToken = "fake-token-0123456789abcdef";

    private readonly TempDir _temp = new();

    public void Dispose() => _temp.Dispose();

    private async Task<(HttpContext Context, ActivityLog Log)> RunAsync(string remoteIp, string? authorization)
    {
        var rig = await StatusRig.CreateAsync(_temp.Combine("rig"), withSave: false);
        var log = new ActivityLog(_temp.Combine("activity.jsonl"));
        var app = new ApplicationBuilder(new ServiceCollection().BuildServiceProvider());
        ApiPipeline.UseGuards(app, new ApiDeps(FakeToken, rig.Query, log, TimeProvider.System));
        app.Run(context =>
        {
            context.Response.StatusCode = StatusCodes.Status200OK;
            return Task.CompletedTask;
        });
        var context = NewContext(remoteIp, authorization);

        await app.Build()(context);
        return (context, log);
    }

    private static DefaultHttpContext NewContext(string remoteIp, string? authorization)
    {
        var context = new DefaultHttpContext();
        context.Connection.RemoteIpAddress = IPAddress.Parse(remoteIp);
        context.Request.Method = "GET";
        context.Request.Path = "/status";
        context.Response.Body = new MemoryStream();
        if (authorization is not null)
        {
            context.Request.Headers.Authorization = authorization;
        }
        return context;
    }

    private static string BodyText(HttpContext context)
    {
        context.Response.Body.Position = 0;
        return new StreamReader(context.Response.Body).ReadToEnd();
    }

    [Theory]
    [InlineData("8.8.8.8", "8.8.8.8")]
    [InlineData("::ffff:8.8.8.8", "8.8.8.8")]
    public async Task Public_client_gets_403_with_contract_body_marker_and_journal_entry(string remoteIp, string loggedClient)
    {
        var (context, log) = await RunAsync(remoteIp, "Bearer " + FakeToken);

        var contract = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "contracts", "api", "error-forbidden.json"));
        Assert.Equal(StatusCodes.Status403Forbidden, context.Response.StatusCode);
        Assert.True(JsonNode.DeepEquals(JsonNode.Parse(contract), JsonNode.Parse(BodyText(context))));
        Assert.Equal("1", context.Response.Headers["X-TotkSync-Api"].ToString());
        var entry = Assert.Single(log.Snapshot());
        Assert.Equal((ActivityKind.Forbidden, 403, loggedClient), (entry.Kind, entry.StatusCode, entry.Client));
    }

    [Fact]
    public async Task Private_client_without_token_gets_401_and_private_client_with_token_passes()
    {
        var (rejected, _) = await RunAsync("192.168.1.23", null);
        var (allowed, _) = await RunAsync("192.168.1.23", "Bearer " + FakeToken);

        Assert.Equal((StatusCodes.Status401Unauthorized, StatusCodes.Status200OK), (rejected.Response.StatusCode, allowed.Response.StatusCode));
    }
}
