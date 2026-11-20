namespace TotkSync.Core.Zip;

internal static class Crc32
{
    private const uint Polynomial = 0xEDB88320;

    private static readonly uint[] Table = BuildTable();

    public static uint Compute(ReadOnlySpan<byte> data)
    {
        var crc = uint.MaxValue;
        foreach (var value in data)
        {
            crc = Table[(crc ^ value) & 0xFF] ^ (crc >> 8);
        }
        return ~crc;
    }

    private static uint[] BuildTable()
    {
        var table = new uint[256];
        for (uint index = 0; index < table.Length; index++)
        {
            table[index] = Enumerable.Range(0, 8)
                .Aggregate(index, (value, _) => (value & 1) != 0 ? Polynomial ^ (value >> 1) : value >> 1);
        }
        return table;
    }
}
