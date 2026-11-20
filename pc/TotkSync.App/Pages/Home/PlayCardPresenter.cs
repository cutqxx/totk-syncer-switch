using TotkSync.Core.Game;

namespace TotkSync.App.Pages.Home;

public sealed record PlayInputs(bool IsConfigured, bool IsSaveMissing, bool IsGameRunning, LaunchPhase Phase);

public static class PlayCardPresenter
{
    public static PlayCardModel From(PlayInputs inputs)
    {
        if (inputs.Phase == LaunchPhase.WaitingForSaveTransfer)
        {
            return new PlayCardModel("Waiting…", PlayIcon.Spinner, false, $"{GameMessages.WaitingForSaveTransfer}. The game starts automatically.");
        }
        if (inputs.Phase == LaunchPhase.Starting)
        {
            return new PlayCardModel("Starting…", PlayIcon.Spinner, false, "");
        }
        if (inputs.IsGameRunning)
        {
            return new PlayCardModel("TotK is running", PlayIcon.GameRunning, false, "Sync is paused until you close the game.");
        }
        return Ready(inputs);
    }

    private static PlayCardModel Ready(PlayInputs inputs)
    {
        if (!inputs.IsConfigured)
        {
            return new PlayCardModel("Play", PlayIcon.Play, false, "Choose the game folder first.");
        }
        var caption = inputs.IsSaveMissing ? "Starting the game once creates the PC save." : "";
        return new PlayCardModel("Play", PlayIcon.Play, true, caption);
    }
}
