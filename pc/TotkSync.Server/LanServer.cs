using System.Net;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace TotkSync.Server;

public sealed class LanServer : IAsyncDisposable
{
    private const long MaxRequestBodyBytes = 64L * 1024 * 1024;

    private readonly WebApplication _app;

    private LanServer(WebApplication app, int port)
    {
        _app = app;
        Port = port;
    }

    public int Port { get; }

    public static async Task<ServerStartResult> StartAsync(IPAddress ip, int port, ApiDeps deps)
    {
        var app = Build(ip, port, deps);
        try
        {
            await app.StartAsync();
            return new ServerStartResult.Running(new LanServer(app, BoundPort(app)));
        }
        catch (Exception error)
        {
            await app.DisposeAsync();
            return StartFailure.Classify(error);
        }
    }

    public Task StopAsync() => _app.StopAsync();

    public async ValueTask DisposeAsync()
    {
        await _app.StopAsync();
        await _app.DisposeAsync();
    }

    private static WebApplication Build(IPAddress ip, int port, ApiDeps deps)
    {
        var builder = WebApplication.CreateSlimBuilder();
        builder.Logging.ClearProviders();
        builder.WebHost.ConfigureKestrel(kestrel => ConfigureKestrel(kestrel, ip, port));
        var app = builder.Build();
        ApiPipeline.Configure(app, deps);
        return app;
    }

    private static void ConfigureKestrel(KestrelServerOptions kestrel, IPAddress ip, int port)
    {
        kestrel.AddServerHeader = false;
        kestrel.Limits.MaxRequestBodySize = MaxRequestBodyBytes;
        kestrel.Listen(ip, port);
    }

    private static int BoundPort(WebApplication app)
    {
        var addresses = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses;
        return new Uri(addresses.First()).Port;
    }
}
