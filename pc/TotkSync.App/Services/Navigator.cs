using TotkSync.App.Pages.Settings;

namespace TotkSync.App.Services;

public sealed class Navigator
{
    private Action<Type>? _navigate;

    public void Attach(Action<Type> navigate) => _navigate = navigate;

    public void ShowSettings() => _navigate?.Invoke(typeof(SettingsPage));
}
