using System.Text.Json;
using TotkSync.Core.Backups;
using TotkSync.Core.Game;
using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Zip;

namespace TotkSync.Core.Sync;

public sealed class SaveReceiver(
    SettingsSession settings,
    IGameProcess game,
    TransferGate gate,
    SaveStatusCache statusCache,
    BackupStore backups,
    MirrorReplacer replacer,
    SyncStateStore syncState,
    TimeProvider time)
{
    public const string UploadInterrupted = "upload interrupted";
    public const string SeveralSaves = "several save folders found: choose one in Settings";

    public async Task<ReceiveResult> ReceiveAsync(PushRequest request, CancellationToken ct)
    {
        var refusal = RefuseBeforeReading(request.ContentLength);
        if (refusal is not null)
        {
            return refusal;
        }
        if (!gate.TryEnter(out var lease))
        {
            return new ReceiveResult.Busy();
        }
        using (lease)
        {
            return await ReceiveHoldingGateAsync(request, ct);
        }
    }

    private ReceiveResult? RefuseBeforeReading(long? contentLength)
    {
        if (contentLength is null)
        {
            return new ReceiveResult.LengthRequired();
        }
        if (contentLength > SaveZipLimits.MaxZipBytes)
        {
            return new ReceiveResult.TooLarge();
        }
        return game.IsRunning() ? new ReceiveResult.GameRunning() : LookUpSaveDir().Refusal;
    }

    private async Task<ReceiveResult> ReceiveHoldingGateAsync(PushRequest request, CancellationToken ct)
    {
        var body = await TryReadBodyAsync(request, ct);
        if (body is null)
        {
            return new ReceiveResult.Failed(UploadInterrupted);
        }
        var validation = await Task.Run(() => SaveZipValidator.Validate(body), CancellationToken.None);
        return validation switch
        {
            ZipValidation.Valid valid => await WriteAsync(valid.Zip, request.IsForced),
            ZipValidation.Invalid invalid => new ReceiveResult.InvalidZip(invalid.Reason),
            _ => throw new InvalidOperationException($"Unknown validation result {validation}"),
        };
    }

    private static async Task<byte[]?> TryReadBodyAsync(PushRequest request, CancellationToken ct)
    {
        try
        {
            return await request.ReadBody(ct);
        }
        catch (Exception error) when (error is IOException or OperationCanceledException)
        {
            return null;
        }
    }

    private async Task<ReceiveResult> WriteAsync(ValidatedSaveZip zip, bool isForced)
    {
        var lookup = game.IsRunning() ? new SaveDirLookup(null, new ReceiveResult.GameRunning()) : LookUpSaveDir();
        if (lookup.Refusal is not null)
        {
            return lookup.Refusal;
        }
        var result = await Task.Run(() => DecideAndWrite(lookup.Dir!, zip, isForced), CancellationToken.None);
        if (result is ReceiveResult.Replaced or ReceiveResult.Unchanged or ReceiveResult.Failed { IsSaveChanged: true })
        {
            await statusCache.RefreshAsync();
        }
        return result;
    }

    private ReceiveResult DecideAndWrite(string saveDir, ValidatedSaveZip zip, bool isForced)
    {
        var incoming = Fingerprint.OfZip(zip);
        var current = TryCapture(saveDir, out var unreadable);
        if (current is null)
        {
            return unreadable!;
        }
        if (incoming == current.Fingerprint)
        {
            return RecordSync(incoming, () => new ReceiveResult.Unchanged(incoming), "the PC save already matches");
        }
        var lastSync = LoadLastSyncOrNull();
        if (!isForced && lastSync?.Fingerprint != current.Fingerprint)
        {
            return new ReceiveResult.Conflict(current.Modified, lastSync?.At);
        }
        return BackupAndReplace(saveDir, zip);
    }

    private static SaveSnapshot? TryCapture(string saveDir, out ReceiveResult.Failed? failure)
    {
        try
        {
            failure = null;
            return SaveSnapshot.Capture(saveDir);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            failure = new ReceiveResult.Failed($"backup failed, PC save not changed: {error.Message}");
            return null;
        }
    }

    private LastSync? LoadLastSyncOrNull()
    {
        try
        {
            return syncState.Load();
        }
        catch (Exception error) when (error is JsonException or IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private ReceiveResult RecordSync(string fingerprint, Func<ReceiveResult> onRecorded, string context)
    {
        try
        {
            syncState.Save(new LastSync(fingerprint, time.GetLocalNow(), SyncDirection.SwitchToPc));
            return onRecorded();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return new ReceiveResult.Failed($"{context}, but the sync state was not saved: {error.Message}");
        }
    }

    private ReceiveResult BackupAndReplace(string saveDir, ValidatedSaveZip zip)
    {
        BackupFile backup;
        try
        {
            backup = backups.CreateBeforeSwitch(saveDir);
        }
        catch (Exception error) when (error is not OperationCanceledException)
        {
            return new ReceiveResult.Failed($"backup failed, PC save not changed: {error.Message}");
        }
        return ReplaceReportingFailure(saveDir, zip, backup);
    }

    private ReceiveResult ReplaceReportingFailure(string saveDir, ValidatedSaveZip zip, BackupFile backup)
    {
        try
        {
            return replacer.Replace(saveDir, zip) switch
            {
                ReplaceResult.Replaced => RecordReplaced(zip, backup),
                ReplaceResult.GameRunning => new ReceiveResult.GameRunning(),
                ReplaceResult.Failed failed => new ReceiveResult.Failed($"{failed.Reason}; the backup is {backup.Name}"),
                var other => throw new InvalidOperationException($"Unknown replace result {other}"),
            };
        }
        catch (Exception error) when (error is not OperationCanceledException)
        {
            return new ReceiveResult.Failed($"couldn't replace the PC save, the backup is {backup.Name}: {error.Message}");
        }
    }

    private ReceiveResult RecordReplaced(ValidatedSaveZip zip, BackupFile backup)
    {
        var fingerprint = Fingerprint.OfZip(zip);
        var result = RecordSync(
            fingerprint,
            () => new ReceiveResult.Replaced(backup.Name, fingerprint),
            $"the PC save was replaced, the backup is {backup.Name}");
        backups.PruneOldBackups(settings.Current.KeepBackups, backup.Name);
        return result is ReceiveResult.Failed failed ? failed with { IsSaveChanged = true } : result;
    }

    private SaveDirLookup LookUpSaveDir()
    {
        var current = settings.Current;
        if (string.IsNullOrWhiteSpace(current.GameDir))
        {
            return new SaveDirLookup(null, new ReceiveResult.NoSave());
        }
        return SaveLocator.Locate(current.GameDir, current.SaveDir) switch
        {
            LocateResult.Found found when SaveLayout.IsSaveDirectory(found.Dir) => new SaveDirLookup(found.Dir, null),
            LocateResult.Several => new SaveDirLookup(null, new ReceiveResult.Failed(SeveralSaves)),
            _ => new SaveDirLookup(null, new ReceiveResult.NoSave()),
        };
    }

    private sealed record SaveDirLookup(string? Dir, ReceiveResult? Refusal);
}
