namespace TotkSync.Core.Zip;

internal static class ZipPathRules
{
    private const string ForbiddenCharacters = "<>:\"\\|?*";

    private static readonly HashSet<string> ReservedDeviceNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };

    public static bool IsSafe(string path)
    {
        return path.Split('/').All(IsSafeSegment);
    }

    private static bool IsSafeSegment(string segment)
    {
        return segment.Length > 0
            && segment is not "." and not ".."
            && segment.All(IsAllowedCharacter)
            && !segment.EndsWith('.')
            && !segment.EndsWith(' ')
            && !ReservedDeviceNames.Contains(BaseName(segment));
    }

    private static bool IsAllowedCharacter(char value)
    {
        return value >= ' ' && value != '\u007f' && !ForbiddenCharacters.Contains(value);
    }

    private static string BaseName(string segment)
    {
        var dot = segment.IndexOf('.');
        return (dot < 0 ? segment : segment[..dot]).TrimEnd(' ');
    }
}
