namespace TotkSync.App.Pages.Home;

public enum SaveCardKind
{
    None,
    Found,
    NotFound,
    Several,
    Unreadable,
    GameRunning,
}

public enum SaveCardIcon
{
    None,
    CheckmarkCircle,
    ArrowSync,
    Info,
    Warning,
    Games,
}

public enum StatusTone
{
    Success,
    Caution,
    Attention,
    Critical,
    Neutral,
}

public sealed record CandidateRow(string Dir, string ModifiedText);

public sealed record SaveCardModel
{
    public static readonly SaveCardModel Empty = new();

    public SaveCardKind Kind { get; init; } = SaveCardKind.None;

    public SaveCardIcon Icon { get; init; } = SaveCardIcon.None;

    public StatusTone Tone { get; init; } = StatusTone.Neutral;

    public string Word { get; init; } = "";

    public string Detail { get; init; } = "";

    public string Folder { get; init; } = "";

    public string Modified { get; init; } = "";

    public string FilesText { get; init; } = "";

    public string FingerprintPreview { get; init; } = "";

    public string FingerprintFull { get; init; } = "";

    public IReadOnlyList<CandidateRow> Candidates { get; init; } = [];

    public bool IsFound => Kind == SaveCardKind.Found;

    public bool IsNotFound => Kind == SaveCardKind.NotFound;

    public bool IsSeveral => Kind == SaveCardKind.Several;

    public bool HasDetail => Detail.Length > 0;

    public bool HasStatus => Kind != SaveCardKind.None;
}
