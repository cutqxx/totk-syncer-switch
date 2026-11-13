using TotkSync.App.Pages.Home;
using TotkSync.Core.Status;

namespace TotkSync.Tests.App;

public class SaveCardPresenterTests
{
    [Fact]
    public void Running_game_shows_TotK_is_running_with_the_deferred_update_hint()
    {
        var card = SaveCardPresenter.From(new SaveStatus.GameRunning());

        Assert.Equal(
            (SaveCardKind.GameRunning, "TotK is running", "Save status updates after the game exits.", true),
            (card.Kind, card.Word, card.Detail, card.HasStatus));
    }
}
