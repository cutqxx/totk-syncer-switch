using TotkSync.Core.Activity;
using Wpf.Ui.Controls;

namespace TotkSync.App.Pages.Activity;

public sealed record ActivityRowModel(
    ActivityEntry Entry,
    string TimeText,
    string Client,
    string Request,
    string ResultText,
    string CodeText,
    SymbolRegular Symbol,
    string AutomationName)
{
    /// <summary>A screen reader names a list row by its item's text, so the row reads as <see cref="AutomationName"/>.</summary>
    public override string ToString() => AutomationName;
}
