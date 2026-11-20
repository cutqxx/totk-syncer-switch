using System.Windows;
using Microsoft.Extensions.DependencyInjection;
using TotkSync.App.Dialogs;
using TotkSync.App.Pages.Activity;
using TotkSync.App.Pages.Backups;
using TotkSync.App.Pages.Home;
using TotkSync.App.Pages.Settings;
using TotkSync.App.Services;
using TotkSync.Core.Pairing;
using Wpf.Ui.Controls;

namespace TotkSync.App.Tests;

public sealed class XamlLoadTests
{
    public static TheoryData<Type> Pages => new()
    {
        typeof(HomePage),
        typeof(SettingsPage),
        typeof(BackupsPage),
        typeof(ActivityPage),
    };

    [Theory]
    [MemberData(nameof(Pages))]
    public void Page_loads_and_lays_out_with_the_app_resources(Type pageType)
    {
        WpfApp.Run(() =>
        {
            var page = (FrameworkElement)WpfApp.Services.GetRequiredService(pageType);

            page.Measure(new Size(1000, 800));
            page.Arrange(new Rect(0, 0, 1000, 800));
            page.UpdateLayout();
        });
    }

    [Fact]
    public void Main_window_loads_with_the_app_resources()
    {
        WpfApp.Run(() => Assert.NotNull(WpfApp.Services.GetRequiredService<MainWindow>()));
    }

    [Fact]
    public void Firewall_help_dialog_gets_the_ContentDialog_look()
    {
        WpfApp.Run(() =>
        {
            var model = new FirewallHelpModel(FirewallCommand.Build(8765), "http://192.168.1.2:8765/status");
            var dialog = new FirewallHelpDialog(
                new ContentDialogHost(),
                model,
                WpfApp.Services.GetRequiredService<ClipboardService>(),
                WpfApp.Services.GetRequiredService<ShellOpener>(),
                WpfApp.Services.GetRequiredService<Toast>());

            AssertHasContentDialogTemplate(dialog);
        });
    }

    [Fact]
    public void Finishing_transfer_dialog_gets_the_ContentDialog_look()
    {
        WpfApp.Run(() => AssertHasContentDialogTemplate(new FinishingTransferDialog(new ContentDialogHost())));
    }

    /// <summary>
    /// WPF-UI keys its ContentDialog style by the ContentDialog type, so a subclass declared in XAML gets no
    /// style unless it names one: then it has no template, no overlay, no centering and no Close button.
    /// </summary>
    private static void AssertHasContentDialogTemplate(ContentDialog dialog)
    {
        dialog.ApplyTemplate();

        Assert.NotNull(dialog.Template);
        Assert.Same(Application.Current.FindResource(typeof(ContentDialog)), dialog.Style);
    }
}
