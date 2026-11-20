using Microsoft.Win32;

namespace TotkSync.App.Services;

public sealed class FolderPicker
{
    public string? Pick(string title)
    {
        var dialog = new OpenFolderDialog { Title = title };
        return dialog.ShowDialog() == true ? dialog.FolderName : null;
    }
}
