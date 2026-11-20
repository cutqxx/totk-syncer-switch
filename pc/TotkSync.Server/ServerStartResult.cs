namespace TotkSync.Server;

public abstract record ServerStartResult
{
    private ServerStartResult()
    {
    }

    public sealed record Running(LanServer Server) : ServerStartResult;

    public sealed record PortBusy : ServerStartResult;

    public sealed record Failed(string Message) : ServerStartResult;
}
