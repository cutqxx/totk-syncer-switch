namespace TotkSync.Core.Zip;

public static class SaveZipLimits
{
    public const long MaxZipBytes = 64L * 1024 * 1024;
    public const long MaxUnpackedBytes = 64L * 1024 * 1024;
    public const int MaxEntries = 1000;
    public const long RatioCheckFromBytes = 4L * 1024 * 1024;
    public const long MaxCompressionRatio = 200;
}
