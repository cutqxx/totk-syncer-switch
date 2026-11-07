using System.Globalization;
using TotkSync.Core.Saves;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

internal static class StatusMapping
{
    private const string AppName = "totk-sync";
    private const int ApiVersion = 1;
    private const string IsoSecondsFormat = "yyyy-MM-dd'T'HH:mm:sszzz";

    public static StatusResponse ToResponse(LauncherStatus status, TimeProvider time)
    {
        return new StatusResponse(
            AppName,
            ApiVersion,
            status.GameRunning,
            ToSaveDto(status.Save, time),
            ToLastSyncDto(status.LastSync, time));
    }

    private static SaveDto ToSaveDto(SaveSnapshot? save, TimeProvider time)
    {
        return save is null
            ? new SaveDto(Present: false)
            : new SaveDto(true, FormatDate(save.Modified, time), save.Files, save.Bytes, save.Fingerprint);
    }

    private static LastSyncDto? ToLastSyncDto(LastSync? lastSync, TimeProvider time)
    {
        return lastSync is null
            ? null
            : new LastSyncDto(lastSync.Fingerprint, FormatDate(lastSync.At, time), lastSync.Direction);
    }

    private static string FormatDate(DateTimeOffset value, TimeProvider time)
    {
        return TimeZoneInfo.ConvertTime(value, time.LocalTimeZone).ToString(IsoSecondsFormat, CultureInfo.InvariantCulture);
    }
}
