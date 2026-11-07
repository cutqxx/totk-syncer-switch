using TotkSync.Core.Saves;
using TotkSync.Core.Sync;

namespace TotkSync.Core.Status;

public sealed record LauncherStatus(bool GameRunning, SaveSnapshot? Save, LastSync? LastSync);
