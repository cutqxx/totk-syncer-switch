using TotkSync.App.Pages.Home;
using TotkSync.Core.Saves;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;

namespace TotkSync.Tests.App;

public class SaveCardPresenterTests
{
    private static readonly DateTimeOffset Moment = new(2026, 11, 3, 12, 0, 0, TimeSpan.Zero);

    private static SaveStatus.Found Found(LastSync? lastSync, string lastFingerprint = "sha256:aaaa")
    {
        return new SaveStatus.Found(new SaveSnapshot(@"C:\save", Moment, 3, 1024, lastFingerprint), lastSync);
    }

    private static LastSync SyncedAt(string fingerprint) => new(fingerprint, Moment, SyncDirection.SwitchToPc);

    [Fact]
    public void Running_game_shows_TotK_is_running_with_the_deferred_update_hint()
    {
        var card = SaveCardPresenter.From(new SaveStatus.GameRunning());

        Assert.Equal(
            (SaveCardKind.GameRunning, "TotK is running", "Save status updates after the game exits.", true),
            (card.Kind, card.Word, card.Detail, card.HasStatus));
    }

    [Fact]
    public void Running_game_uses_the_Games_icon_with_neutral_tone()
    {
        var card = SaveCardPresenter.From(new SaveStatus.GameRunning());

        Assert.Equal((SaveCardIcon.Games, StatusTone.Neutral), (card.Icon, card.Tone));
    }

    [Fact]
    public void In_sync_save_uses_the_check_icon_with_success_tone()
    {
        var card = SaveCardPresenter.From(Found(SyncedAt("sha256:aaaa")));

        Assert.Equal((SaveCardIcon.CheckmarkCircle, StatusTone.Success, "In sync"), (card.Icon, card.Tone, card.Word));
    }

    [Fact]
    public void Save_with_new_progress_uses_the_sync_icon_with_attention_tone()
    {
        var card = SaveCardPresenter.From(Found(SyncedAt("sha256:other")));

        Assert.Equal((SaveCardIcon.ArrowSync, StatusTone.Attention, "PC has new progress"), (card.Icon, card.Tone, card.Word));
    }

    [Fact]
    public void Never_synced_save_uses_the_info_icon_with_neutral_tone()
    {
        var card = SaveCardPresenter.From(Found(null));

        Assert.Equal((SaveCardIcon.Info, StatusTone.Neutral, "Never synced"), (card.Icon, card.Tone, card.Word));
    }

    [Fact]
    public void Several_saves_not_found_and_unreadable_all_use_the_warning_icon_with_caution_tone()
    {
        var several = SaveCardPresenter.From(new SaveStatus.Several([new SaveCandidate(@"C:\a", Moment)]));
        var notFound = SaveCardPresenter.From(new SaveStatus.NotFound());
        var unreadable = SaveCardPresenter.From(new SaveStatus.Unreadable("denied"));

        Assert.All([several, notFound, unreadable], card => Assert.Equal((SaveCardIcon.Warning, StatusTone.Caution), (card.Icon, card.Tone)));
    }

    [Fact]
    public void Not_configured_shows_no_status_and_no_icon()
    {
        var card = SaveCardPresenter.From(new SaveStatus.NotConfigured());

        Assert.Equal((SaveCardIcon.None, false), (card.Icon, card.HasStatus));
    }
}
