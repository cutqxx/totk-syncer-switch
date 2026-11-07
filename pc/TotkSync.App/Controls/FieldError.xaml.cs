using System.Windows;
using System.Windows.Controls;

namespace TotkSync.App.Controls;

public partial class FieldError : UserControl
{
    public static readonly DependencyProperty MessageProperty =
        DependencyProperty.Register(
            nameof(Message),
            typeof(string),
            typeof(FieldError),
            new PropertyMetadata("", OnMessageChanged));

    public FieldError()
    {
        InitializeComponent();
        Visibility = Visibility.Collapsed;
    }

    public string Message
    {
        get => (string)GetValue(MessageProperty);
        set => SetValue(MessageProperty, value);
    }

    private static void OnMessageChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        var isEmpty = string.IsNullOrEmpty((string?)args.NewValue);
        ((FieldError)sender).Visibility = isEmpty ? Visibility.Collapsed : Visibility.Visible;
    }
}
