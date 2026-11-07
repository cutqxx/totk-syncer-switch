using System.Windows;

namespace TotkSync.App.Services;

public sealed class UiThread
{
    public void Post(Action action)
    {
        Application.Current.Dispatcher.InvokeAsync(action);
    }
}
