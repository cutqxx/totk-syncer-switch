using System.Net;
using TotkSync.Core.Activity;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;

namespace TotkSync.Server;

public enum ServerPhase
{
    Starting,
    Running,
    PortBusy,
    Failed,
    Stopping,
}

public sealed record ServerState(ServerPhase Phase, int Port, string Message, IReadOnlyList<IPAddress> Addresses)
{
    public static ServerState Starting(int port) => new(ServerPhase.Starting, port, "", []);
}

public sealed class ServerHost(
    SettingsSession settings,
    StatusQuery status,
    ActivityLog activity,
    TimeProvider time,
    IPAddress listenAddress,
    Func<IReadOnlyList<IPAddress>> detectLanAddresses)
{
    private LanServer? _server;

    public ServerState State { get; private set; } = ServerState.Starting(settings.Current.Port);

    public event Action? Changed;

    public async Task StartAsync()
    {
        var port = settings.Current.Port;
        Publish(ServerState.Starting(port));
        var addresses = await Task.Run(detectLanAddresses);
        var deps = new ApiDeps(settings.Current.Token ?? "", status, activity, time);
        var result = await LanServer.StartAsync(listenAddress, port, deps);
        Publish(ToState(result, port, addresses));
    }

    public async Task StopAsync()
    {
        var server = _server;
        _server = null;
        if (server is null)
        {
            return;
        }
        Publish(State with { Phase = ServerPhase.Stopping });
        await server.DisposeAsync();
    }

    private ServerState ToState(ServerStartResult result, int port, IReadOnlyList<IPAddress> addresses)
    {
        switch (result)
        {
            case ServerStartResult.Running running:
                _server = running.Server;
                return new ServerState(ServerPhase.Running, port, "", addresses);
            case ServerStartResult.PortBusy:
                return new ServerState(ServerPhase.PortBusy, port, ServerErrorText.PortBusy(port), []);
            case ServerStartResult.Failed failed:
                return new ServerState(ServerPhase.Failed, port, ServerErrorText.CannotListen(port, failed.Message), []);
            default:
                throw new InvalidOperationException($"Unknown start result {result}");
        }
    }

    private void Publish(ServerState state)
    {
        State = state;
        Changed?.Invoke();
    }
}
