using System.Net;
using TotkSync.Core.Activity;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;

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
    Func<IReadOnlyList<IPAddress>> detectLanAddresses,
    SaveReceiver? receiver = null,
    Action<Exception>? onServerError = null)
{
    private readonly SemaphoreSlim _lifecycle = new(1, 1);
    private LanServer? _server;
    private bool _isStopRequested;

    public ServerState State { get; private set; } = ServerState.Starting(settings.Current.Port);

    public event Action? Changed;

    public async Task StartAsync()
    {
        await _lifecycle.WaitAsync();
        try
        {
            _isStopRequested = false;
            if (_server is null)
            {
                await StartCoreAsync();
            }
        }
        finally
        {
            _lifecycle.Release();
        }
    }

    public async Task StopAsync()
    {
        await _lifecycle.WaitAsync();
        try
        {
            _isStopRequested = true;
            await StopCoreAsync();
        }
        finally
        {
            _lifecycle.Release();
        }
    }

    public async Task RestartIfPortChangedAsync()
    {
        await _lifecycle.WaitAsync();
        try
        {
            if (IsRunningOnStalePort())
            {
                await StopCoreAsync();
                await StartCoreAsync();
            }
        }
        finally
        {
            _lifecycle.Release();
        }
    }

    private bool IsRunningOnStalePort()
    {
        var hasSettled = State.Phase is ServerPhase.Running or ServerPhase.PortBusy or ServerPhase.Failed;
        return !_isStopRequested && hasSettled && State.Port != settings.Current.Port;
    }

    private async Task StartCoreAsync()
    {
        var port = settings.Current.Port;
        Publish(ServerState.Starting(port));
        var addresses = await Task.Run(detectLanAddresses);
        var deps = new ApiDeps(settings.Current.Token ?? "", status, activity, time, receiver, onServerError);
        var result = await LanServer.StartAsync(listenAddress, port, deps);
        Publish(ToState(result, port, addresses));
    }

    private async Task StopCoreAsync()
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
