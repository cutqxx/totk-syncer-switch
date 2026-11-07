using TotkSync.Core.Game;

namespace TotkSync.Core.Status;

public sealed class StatusQuery(SaveStatusCache cache, IGameProcess game)
{
    public LauncherStatus Get()
    {
        var found = cache.Current as SaveStatus.Found;
        return new LauncherStatus(game.IsRunning(), found?.Snapshot, found?.LastSync);
    }
}
