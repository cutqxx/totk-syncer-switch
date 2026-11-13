namespace TotkSync.Core.Backups;

public sealed record BackupEntry(string Name, DateTime LocalTime, string Kind, long Bytes);
