using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using TotkSync.Core.Settings;

namespace TotkSync.Core.Activity;

public sealed class ActivityLog(string path, int capacity = ActivityLog.DefaultCapacity)
{
    public const int DefaultCapacity = 500;

    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.SnakeCaseLower) },
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
    };

    private readonly object _sync = new();
    private readonly List<ActivityEntry> _entries = [];
    private bool _isLoading;
    private bool _hasUnsavedAppends;

    public event Action<ActivityEntry>? EntryAdded;

    public async Task<LoadResult> LoadAsync()
    {
        lock (_sync)
        {
            _isLoading = true;
        }
        var stored = await Task.Run(ReadStoredEntries);
        return CompleteLoad(stored);
    }

    public void Append(ActivityEntry entry)
    {
        lock (_sync)
        {
            _entries.Add(entry);
            TrimToCapacity();
            PersistUnlessLoading();
        }
        EntryAdded?.Invoke(entry);
    }

    public IReadOnlyList<ActivityEntry> Snapshot()
    {
        lock (_sync)
        {
            return _entries.ToList();
        }
    }

    private StoredEntries ReadStoredEntries()
    {
        try
        {
            return new StoredEntries(ReadFile(), null);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
        {
            return new StoredEntries([], error.Message);
        }
    }

    private List<ActivityEntry> ReadFile()
    {
        if (!File.Exists(path))
        {
            return [];
        }
        return File.ReadLines(path, Encoding.UTF8)
            .Where(line => !string.IsNullOrWhiteSpace(line))
            .Select(line => JsonSerializer.Deserialize<ActivityEntry>(line, Options) ?? throw new JsonException("Empty entry"))
            .TakeLast(capacity)
            .ToList();
    }

    private LoadResult CompleteLoad(StoredEntries stored)
    {
        lock (_sync)
        {
            _entries.InsertRange(0, stored.Entries);
            TrimToCapacity();
            _isLoading = false;
            PersistIfAppendedWhileLoading();
        }
        return stored.FailureReason is null
            ? new LoadResult.Loaded(stored.Entries.Count)
            : new LoadResult.Unreadable(stored.FailureReason);
    }

    private void PersistIfAppendedWhileLoading()
    {
        if (_hasUnsavedAppends)
        {
            _hasUnsavedAppends = false;
            PersistUnlessLoading();
        }
    }

    private void TrimToCapacity()
    {
        if (_entries.Count > capacity)
        {
            _entries.RemoveRange(0, _entries.Count - capacity);
        }
    }

    private void PersistUnlessLoading()
    {
        if (_isLoading)
        {
            _hasUnsavedAppends = true;
            return;
        }
        try
        {
            AtomicFile.WriteAllBytes(path, SerializeEntries());
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            // History is best effort: a failed write must not fail the request being logged.
        }
    }

    private byte[] SerializeEntries()
    {
        var lines = _entries.Select(entry => JsonSerializer.Serialize(entry, Options) + "\n");
        return Encoding.UTF8.GetBytes(string.Concat(lines));
    }

    private sealed record StoredEntries(IReadOnlyList<ActivityEntry> Entries, string? FailureReason);
}
