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
    string AutomationName);
