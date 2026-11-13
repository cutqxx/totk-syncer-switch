using System.Text.Json;
using TotkSync.Core.Settings;

namespace TotkSync.Core.Sync;

public sealed class SyncStateStore(string path)
{
    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
    };

    public LastSync? Load()
    {
        if (!File.Exists(path))
        {
            return null;
        }
        return JsonSerializer.Deserialize<StateFile>(File.ReadAllBytes(path), Options)?.LastSync;
    }

    public void Save(LastSync lastSync)
    {
        AtomicFile.WriteAllBytes(path, JsonSerializer.SerializeToUtf8Bytes(new StateFile(lastSync), Options));
    }

    private sealed record StateFile(LastSync? LastSync);
}
