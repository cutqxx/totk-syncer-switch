using TotkSync.App.Pages.Home;
using TotkSync.Core.Game;

namespace TotkSync.Tests.App;

public class PlayCardPresenterTests
{
    private static PlayCardModel Card(bool isConfigured = true, bool isSaveMissing = false, bool isGameRunning = false, LaunchPhase phase = LaunchPhase.Idle)
    {
        return PlayCardPresenter.From(new PlayInputs(isConfigured, isSaveMissing, isGameRunning, phase));
    }

    [Fact]
    public void Ready_state_offers_an_enabled_Play_button_with_no_caption()
    {
        var card = Card();

        Assert.Equal(("Play", PlayIcon.Play, true, ""), (card.Label, card.Icon, card.IsEnabled, card.Caption));
        Assert.False(card.HasCaption);
    }

    [Fact]
    public void Missing_pc_save_keeps_Play_enabled_and_explains_that_starting_the_game_creates_it()
    {
        var card = Card(isSaveMissing: true);

        Assert.Equal(("Play", true, "Starting the game once creates the PC save."), (card.Label, card.IsEnabled, card.Caption));
    }

    [Fact]
    public void Running_game_shows_disabled_TotK_is_running()
    {
        var card = Card(isGameRunning: true);

        Assert.Equal(("TotK is running", PlayIcon.GameRunning, false), (card.Label, card.Icon, card.IsEnabled));
        Assert.True(card.IsGameRunningIcon);
    }

    [Fact]
    public void Waiting_for_a_save_transfer_is_disabled_and_shows_the_waiting_message()
    {
        var card = Card(phase: LaunchPhase.WaitingForSaveTransfer);

        Assert.False(card.IsEnabled);
        Assert.True(card.IsSpinner);
        Assert.Contains("Waiting for save transfer to finish", card.Caption);
    }

    [Fact]
    public void Starting_is_disabled_with_a_spinner()
    {
        var card = Card(phase: LaunchPhase.Starting);

        Assert.Equal((false, true), (card.IsEnabled, card.IsSpinner));
    }

    [Fact]
    public void Missing_game_folder_disables_Play_and_asks_to_choose_the_folder()
    {
        var card = Card(isConfigured: false);

        Assert.Equal(("Play", false, "Choose the game folder first."), (card.Label, card.IsEnabled, card.Caption));
    }

    [Fact]
    public void Waiting_wins_over_game_running()
    {
        var card = Card(isGameRunning: true, phase: LaunchPhase.WaitingForSaveTransfer);

        Assert.Contains("Waiting for save transfer", card.Caption);
    }

    [Fact]
    public void Only_the_ready_label_has_an_Alt_accelerator()
    {
        Assert.Equal(("_Play", "TotK is running"), (Card().AccessLabel, Card(isGameRunning: true).AccessLabel));
    }
}
