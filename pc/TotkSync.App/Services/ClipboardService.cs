using System.IO;
using System.Runtime.InteropServices;
using System.Windows;

namespace TotkSync.App.Services;

public sealed class ClipboardService
{
    private const string ExcludeFromMonitorFormat = "ExcludeClipboardContentFromMonitorProcessing";
    private const string ExcludeFromHistoryFormat = "CanIncludeInClipboardHistory";
    private const string ExcludeFromCloudFormat = "CanUploadToCloudClipboard";

    public bool TryCopy(string text)
    {
        try
        {
            Clipboard.SetText(text);
            return true;
        }
        catch (ExternalException)
        {
            return false;
        }
    }

    public bool TryCopySecret(string text)
    {
        try
        {
            Clipboard.SetDataObject(SecretDataObject(text), copy: true);
            return true;
        }
        catch (ExternalException)
        {
            return false;
        }
    }

    private static DataObject SecretDataObject(string text)
    {
        var data = new DataObject();
        data.SetText(text);
        data.SetData(ExcludeFromMonitorFormat, new MemoryStream(BitConverter.GetBytes(1)));
        data.SetData(ExcludeFromHistoryFormat, new MemoryStream(BitConverter.GetBytes(0)));
        data.SetData(ExcludeFromCloudFormat, new MemoryStream(BitConverter.GetBytes(0)));
        return data;
    }
}
