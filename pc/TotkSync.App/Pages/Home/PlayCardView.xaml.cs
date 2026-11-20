using System.Windows.Controls;

namespace TotkSync.App.Pages.Home;

public partial class PlayCardView : UserControl
{
    public PlayCardView()
    {
        InitializeComponent();
    }

    public bool FocusPlayWhenEnabled() => PlayButton.IsEnabled && PlayButton.Focus();
}
