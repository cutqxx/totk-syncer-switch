using System.Security.Cryptography;

namespace TotkSync.Core.Settings;

public static class TokenGenerator
{
    private const int TokenByteCount = 32;

    public static string Generate()
    {
        return Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(TokenByteCount));
    }
}
