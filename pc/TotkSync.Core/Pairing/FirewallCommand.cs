namespace TotkSync.Core.Pairing;

public static class FirewallCommand
{
    public const string RuleName = "TotK Save Sync";

    public static string Build(int port)
    {
        return $"netsh advfirewall firewall add rule name=\"{RuleName}\" dir=in action=allow protocol=TCP localport={port} profile=private";
    }
}
