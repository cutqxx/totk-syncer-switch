using TotkSync.App.Dialogs;
using TotkSync.Core.Pairing;
using TotkSync.Core.Settings;
using Wpf.Ui;

namespace TotkSync.App.Services;

public sealed class FirewallHelpPresenter(
    IContentDialogService dialogs,
    SettingsSession settings,
    ClipboardService clipboard,
    ShellOpener shell,
    Toast toast)
{
    public async Task ShowAsync(ServerAddress? address)
    {
        var checkUrl = address is null ? null : $"{address.Url}/status";
        var model = new FirewallHelpModel(FirewallCommand.Build(settings.Current.Port), checkUrl);
        var dialog = new FirewallHelpDialog(dialogs.GetDialogHostEx(), model, clipboard, shell, toast);
        await dialog.ShowAsync();
    }
}
