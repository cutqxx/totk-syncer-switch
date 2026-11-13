namespace Wpf.Ui.Controls;

/// <summary>
/// Stand-in for the WPF-UI icon enum so the WPF-free SaveCardPresenter/SaveCardModel compile on plain net10.0.
/// Only the members the presenter uses; add one here when the presenter starts using another.
/// </summary>
public enum SymbolRegular
{
    Empty,
    CheckmarkCircle20,
    ArrowSync20,
    Info20,
    Warning20,
    Games20,
}
