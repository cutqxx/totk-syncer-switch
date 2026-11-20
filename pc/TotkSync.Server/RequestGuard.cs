using System.Net;
using System.Security.Cryptography;
using System.Text;
using TotkSync.Core.Pairing;

namespace TotkSync.Server;

public enum GuardResult
{
    Allowed,
    Forbidden,
    Unauthorized,
}

public static class RequestGuard
{
    private const string BearerPrefix = "Bearer ";

    public static GuardResult Check(IPAddress remote, string? authorization, ReadOnlySpan<byte> token)
    {
        if (!PrivateNetwork.IsPrivate(remote))
        {
            return GuardResult.Forbidden;
        }
        return HasValidBearerToken(authorization, token) ? GuardResult.Allowed : GuardResult.Unauthorized;
    }

    private static bool IsBlank(ReadOnlySpan<byte> token)
    {
        return string.IsNullOrWhiteSpace(Encoding.UTF8.GetString(token));
    }

    private static bool HasValidBearerToken(string? authorization, ReadOnlySpan<byte> token)
    {
        if (IsBlank(token) || authorization is null || !authorization.StartsWith(BearerPrefix, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }
        var presented = Encoding.UTF8.GetBytes(authorization[BearerPrefix.Length..]);
        return CryptographicOperations.FixedTimeEquals(presented, token);
    }
}
