using Microsoft.Win32;

namespace TotkSync.App.Services;

public sealed class SaveFilePicker
{
    public string? Pick(string title, string defaultFileName)
    {
        var dialog = new SaveFileDialog
        {
            Title = title,
            FileName = defaultFileName,
            DefaultExt = ".json",
            Filter = "JSON file (*.json)|*.json",
            AddExtension = true,
            OverwritePrompt = true,
        };
        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }
}
