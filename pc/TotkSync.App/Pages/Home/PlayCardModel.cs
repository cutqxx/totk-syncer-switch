namespace TotkSync.App.Pages.Home;

public enum PlayIcon
{
    Play,
    Spinner,
    GameRunning,
}

public sealed record PlayCardModel(string Label, PlayIcon Icon, bool IsEnabled, string Caption)
{
    private const string ReadyLabel = "Play";

    public string AccessLabel => Label == ReadyLabel ? "_" + Label : Label;

    public bool HasCaption => Caption.Length > 0;

    public bool IsPlayIcon => Icon == PlayIcon.Play;

    public bool IsGameRunningIcon => Icon == PlayIcon.GameRunning;

    public bool IsSpinner => Icon == PlayIcon.Spinner;
}
