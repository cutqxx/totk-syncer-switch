using TotkSync.Core.Display;
using TotkSync.Core.Saves;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;

namespace TotkSync.App.Pages.Home;

public static class SaveCardPresenter
{
    public static SaveCardModel From(SaveStatus status)
    {
        return status switch
        {
            SaveStatus.Found found => FromFound(found),
            SaveStatus.Several several => FromSeveral(several),
            SaveStatus.NotFound => FromNotFound(),
            SaveStatus.Unreadable unreadable => FromUnreadable(unreadable),
            SaveStatus.GameRunning => FromGameRunning(),
            _ => SaveCardModel.Empty,
        };
    }

    private static SaveCardModel FromFound(SaveStatus.Found found)
    {
        var snapshot = found.Snapshot;
        return SyncLook(found) with
        {
            Kind = SaveCardKind.Found,
            Folder = snapshot.Dir,
            Modified = DisplayFormat.Date(snapshot.Modified),
            FilesText = DisplayFormat.FilesAndSize(snapshot.Files, snapshot.Bytes),
            FingerprintPreview = DisplayFormat.FingerprintPreview(snapshot.Fingerprint),
            FingerprintFull = snapshot.Fingerprint,
        };
    }

    private static SaveCardModel SyncLook(SaveStatus.Found found)
    {
        var lastSyncText = DisplayFormat.LastSyncText(found.LastSync);
        return found.Sync switch
        {
            SyncStatus.InSync => new SaveCardModel
            {
                Icon = SaveCardIcon.CheckmarkCircle,
                Tone = StatusTone.Success,
                Word = "In sync",
                Detail = lastSyncText,
            },
            SyncStatus.PcHasNewProgress => new SaveCardModel
            {
                Icon = SaveCardIcon.ArrowSync,
                Tone = StatusTone.Attention,
                Word = "PC has new progress",
                Detail = $"{lastSyncText}\nUse \"Get from PC\" on Switch to continue there.",
            },
            _ => new SaveCardModel
            {
                Icon = SaveCardIcon.Info,
                Tone = StatusTone.Neutral,
                Word = "Never synced",
                Detail = "Use \"Send to PC\" or \"Get from PC\" on Switch.",
            },
        };
    }

    private static SaveCardModel FromSeveral(SaveStatus.Several several)
    {
        return new SaveCardModel
        {
            Kind = SaveCardKind.Several,
            Icon = SaveCardIcon.Warning,
            Tone = StatusTone.Caution,
            Word = "Several save folders found",
            Detail = "Choose which one TotK Save Sync should use.",
            Candidates = several.Items.Select(ToRow).ToList(),
        };
    }

    private static CandidateRow ToRow(SaveCandidate candidate)
    {
        return new CandidateRow(candidate.Dir, $"Modified {DisplayFormat.Date(candidate.Modified)}");
    }

    private static SaveCardModel FromNotFound()
    {
        return new SaveCardModel
        {
            Kind = SaveCardKind.NotFound,
            Icon = SaveCardIcon.Warning,
            Tone = StatusTone.Caution,
            Word = "PC save not found",
            Detail = SaveLocator.NotFoundMessage,
        };
    }

    private static SaveCardModel FromGameRunning()
    {
        return new SaveCardModel
        {
            Kind = SaveCardKind.GameRunning,
            Icon = SaveCardIcon.Games,
            Tone = StatusTone.Neutral,
            Word = "TotK is running",
            Detail = "Save status updates after the game exits.",
        };
    }

    private static SaveCardModel FromUnreadable(SaveStatus.Unreadable unreadable)
    {
        return new SaveCardModel
        {
            Kind = SaveCardKind.Unreadable,
            Icon = SaveCardIcon.Warning,
            Tone = StatusTone.Caution,
            Word = "Couldn't read the PC save",
            Detail = unreadable.Reason,
        };
    }
}
