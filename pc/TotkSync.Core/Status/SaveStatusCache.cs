using TotkSync.Core.Settings;

namespace TotkSync.Core.Status;

public sealed class SaveStatusCache
{
    private readonly SettingsSession _settings;
    private readonly SaveStatusReader _reader;
    private int _latestRequest;
    private SaveStatus? _current;

    public SaveStatusCache(SettingsSession settings, SaveStatusReader reader)
    {
        _settings = settings;
        _reader = reader;
        _settings.Changed += () => _ = RefreshAsync();
    }

    public SaveStatus? Current => Volatile.Read(ref _current);

    public event Action? Changed;

    public async Task RefreshAsync()
    {
        var request = ++_latestRequest;
        var snapshot = _settings.Current;
        var status = await Task.Run(() => _reader.Read(snapshot));
        if (request != _latestRequest)
        {
            return;
        }
        Volatile.Write(ref _current, status);
        Changed?.Invoke();
    }
}
