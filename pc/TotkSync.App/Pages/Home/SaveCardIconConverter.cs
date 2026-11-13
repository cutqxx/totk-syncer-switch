using System.Globalization;
using System.Windows.Data;
using Wpf.Ui.Controls;

namespace TotkSync.App.Pages.Home;

public sealed class SaveCardIconConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        return value switch
        {
            SaveCardIcon.CheckmarkCircle => SymbolRegular.CheckmarkCircle20,
            SaveCardIcon.ArrowSync => SymbolRegular.ArrowSync20,
            SaveCardIcon.Info => SymbolRegular.Info20,
            SaveCardIcon.Warning => SymbolRegular.Warning20,
            SaveCardIcon.Games => SymbolRegular.Games20,
            _ => SymbolRegular.Empty,
        };
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotSupportedException();
    }
}
