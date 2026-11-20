using System.Net.Sockets;

namespace TotkSync.Server;

internal static class StartFailure
{
    public static ServerStartResult Classify(Exception error)
    {
        var socketError = FindSocketError(error);
        return socketError?.SocketErrorCode == SocketError.AddressAlreadyInUse
            ? new ServerStartResult.PortBusy()
            : new ServerStartResult.Failed((socketError ?? error).Message);
    }

    private static SocketException? FindSocketError(Exception error)
    {
        for (var current = error; current is not null; current = current.InnerException)
        {
            if (current is SocketException socketError)
            {
                return socketError;
            }
        }
        return null;
    }
}
