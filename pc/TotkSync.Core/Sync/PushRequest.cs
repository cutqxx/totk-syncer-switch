namespace TotkSync.Core.Sync;

public sealed record PushRequest(long? ContentLength, Func<CancellationToken, Task<byte[]>> ReadBody);
