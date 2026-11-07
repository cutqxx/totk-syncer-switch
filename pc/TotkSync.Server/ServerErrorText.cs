namespace TotkSync.Server;

public static class ServerErrorText
{
    public static string PortBusy(int port)
    {
        return $"Error: port {port} is busy (is the launcher already running?)";
    }

    public static string CannotListen(int port, string systemMessage)
    {
        return $"Error: can't listen on port {port} ({systemMessage})";
    }
}
