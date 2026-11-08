using System.Buffers.Binary;
using System.Text;
using TotkSync.Core.Saves;

namespace TotkSync.Core.Zip;

internal static class SaveMeta
{
    private static readonly byte[] Magic = Encoding.ASCII.GetBytes("JKSV");
    private const int TitleIdOffset = 5;
    private const int MinimumLength = TitleIdOffset + sizeof(ulong);

    public static bool IsForTotk(ReadOnlySpan<byte> content)
    {
        return content.Length >= MinimumLength
            && content[..Magic.Length].SequenceEqual(Magic)
            && BinaryPrimitives.ReadUInt64LittleEndian(content[TitleIdOffset..]) == SaveLayout.TitleId;
    }
}
