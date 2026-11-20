namespace TotkSync.Core.Saves;

public sealed record SaveSnapshot(string Dir, DateTimeOffset Modified, int Files, long Bytes, string Fingerprint)
{
    public static SaveSnapshot Capture(string dir)
    {
        var files = SaveFile.Enumerate(dir);
        return new SaveSnapshot(
            dir,
            SaveFile.LatestModified(files, dir),
            files.Count,
            files.Sum(file => file.Length),
            Saves.Fingerprint.OfFiles(files));
    }
}
