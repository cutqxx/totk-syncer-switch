using TotkSync.Core.Game;
using TotkSync.Core.Settings;

namespace TotkSync.Core.Status;

public sealed class SaveStatusCache
{
    private readonly SettingsSession _settings;
    private readonly SaveStatusReader _reader;
    private readonly IGameProcess? _game;
    private int _latestRequest;
    private SaveStatus? _current;

    public SaveStatusCache(SettingsSession settings, SaveStatusReader reader, IGameProcess? game = null)
    {
        _game = game;
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
        var status = await Task.Run(() => ReadUnlessGameRunning(snapshot));
        if (status is null)
        {
            return;
        }
        if (request != _latestRequest)
        {
            return;
        }
        Volatile.Write(ref _current, status);
        Changed?.Invoke();
    }

    private SaveStatus? ReadUnlessGameRunning(LauncherSettings settings)
    {
        if (_game?.IsRunning() != true || string.IsNullOrWhiteSpace(settings.GameDir))
        {
            return _reader.Read(settings);
        }
        return Current is null ? new SaveStatus.GameRunning() : null;
    }
}
