using Wpf.Ui.Controls;

namespace TotkSync.App.Pages.Home;

public sealed record ServerCardModel
{
    public static readonly ServerCardModel Starting = new()
    {
        IsBusy = true,
        Word = "Starting…",
    };

    public SymbolRegular Symbol { get; init; } = SymbolRegular.Empty;

    public StatusTone Tone { get; init; } = StatusTone.Neutral;

    public string Word { get; init; } = "";

    public string Detail { get; init; } = "";

    public bool IsBusy { get; init; }

    public bool IsError { get; init; }

    public IReadOnlyList<string> Addresses { get; init; } = [];

    public bool HasDetail => Detail.Length > 0;

    public bool HasSingleAddress => Addresses.Count == 1;

    public bool HasSeveralAddresses => Addresses.Count > 1;

    public string FirstAddress => Addresses.Count > 0 ? Addresses[0] : "";
}
