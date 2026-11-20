namespace TotkSync.Core.Activity;

public sealed record ActivityEntry(
    DateTimeOffset Time,
    string Client,
    string Method,
    string Path,
    int StatusCode,
    ActivityKind Kind);
