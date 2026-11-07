using TotkSync.Core.Activity;
using TotkSync.Core.Display;
using Wpf.Ui.Controls;

namespace TotkSync.App.Pages.Activity;

public static class ActivityPresenter
{
    public static ActivityRowModel ToRow(ActivityEntry entry)
    {
        var timeText = DisplayFormat.DateWithSeconds(entry.Time);
        var request = $"{entry.Method} {entry.Path}";
        var resultText = ResultText(entry.Kind);
        return new ActivityRowModel(
            entry,
            timeText,
            entry.Client,
            request,
            resultText,
            entry.StatusCode.ToString(),
            Symbol(entry.Kind),
            $"{timeText}, {entry.Client}, {request}, {resultText}");
    }

    private static string ResultText(ActivityKind kind)
    {
        return kind switch
        {
            ActivityKind.StatusOk => "OK",
            ActivityKind.Unauthorized => "Rejected: wrong or missing token",
            ActivityKind.Forbidden => "Rejected: not a private network address",
            ActivityKind.NotFound => "Not found",
            ActivityKind.MethodNotAllowed => "Method not allowed",
            ActivityKind.Internal => "Launcher error",
            ActivityKind.Other => "Other response",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, null),
        };
    }

    private static SymbolRegular Symbol(ActivityKind kind)
    {
        return kind == ActivityKind.StatusOk ? SymbolRegular.CheckmarkCircle16 : SymbolRegular.ErrorCircle16;
    }
}
