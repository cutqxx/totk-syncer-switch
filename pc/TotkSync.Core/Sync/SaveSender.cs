using TotkSync.Core.Game;
using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Settings;
using TotkSync.Core.Status;
using TotkSync.Core.Zip;

namespace TotkSync.Core.Sync;

/// <summary>
/// Hands the PC save to the Switch (GET /save) and records the Switch's confirmation (POST /save/ack).
/// Both hold the transfer gate, so a save being replaced or launched is never read half-way.
/// Sending never changes the save folder or the sync state.
/// </summary>
public sealed class SaveSender(
    SettingsSession settings,
    IGameProcess game,
    TransferGate gate,
    SaveStatusCache statusCache,
    SyncStateStore syncState,
    TimeProvider time,
    PendingRecovery? recovery = null)
{
    public SendResult Send()
    {
        if (!gate.TryEnter(out var lease))
        {
            return new SendResult.Busy();
        }
        using (lease)
        {
            recovery?.RunHoldingGate();
            if (game.IsRunning())
            {
                return new SendResult.GameRunning();
            }
            var lookup = SaveDirLookup.For(settings.Current);
            return lookup switch
            {
                { Dir: { } dir } => Pack(dir),
                { HasSeveral: true } => new SendResult.Failed(SaveReceiver.SeveralSaves),
                _ => new SendResult.NoSave(),
            };
        }
    }

    public async Task<AckResult> AckAsync(string fingerprint)
    {
        if (!gate.TryEnter(out var lease))
        {
            return new AckResult.Busy();
        }
        AckResult result;
        using (lease)
        {
            result = await Task.Run(() => AckHoldingGate(fingerprint));
        }
        if (result is AckResult.Recorded)
        {
            await statusCache.RefreshAsync();
        }
        return result;
    }

    private AckResult AckHoldingGate(string fingerprint)
    {
        if (game.IsRunning())
        {
            return new AckResult.GameRunning();
        }
        var lookup = SaveDirLookup.For(settings.Current);
        if (lookup.Dir is null)
        {
            return lookup.HasSeveral ? new AckResult.Failed(SaveReceiver.SeveralSaves) : new AckResult.NoSave();
        }
        try
        {
            if (Fingerprint.OfDirectory(lookup.Dir) != fingerprint)
            {
                return new AckResult.Mismatch();
            }
            syncState.Save(new LastSync(fingerprint, time.GetLocalNow(), SyncDirection.PcToSwitch));
            return new AckResult.Recorded();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return new AckResult.Failed($"the sync was not recorded: {error.Message}");
        }
    }

    private static SendResult Pack(string saveDir)
    {
        try
        {
            var modified = SaveFile.LatestModified(SaveFile.Enumerate(saveDir), saveDir);
            var zip = SaveZipPacker.Pack(saveDir);
            return SaveZipValidator.Validate(zip) switch
            {
                ZipValidation.Valid valid => new SendResult.Sent(zip, Fingerprint.OfZip(valid.Zip), modified),
                ZipValidation.Invalid invalid => new SendResult.Failed($"the PC save can't be sent: {invalid.Reason}"),
                var other => throw new InvalidOperationException($"Unknown validation result {other}"),
            };
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return new SendResult.Failed($"couldn't read the PC save: {error.Message}");
        }
    }
}
