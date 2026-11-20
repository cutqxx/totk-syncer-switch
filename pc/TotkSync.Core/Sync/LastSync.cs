using System.Text.Json;
using System.Text.Json.Serialization;

namespace TotkSync.Core.Sync;

[JsonConverter(typeof(SyncDirectionConverter))]
public enum SyncDirection
{
    SwitchToPc,
    PcToSwitch,
}

public sealed record LastSync(string Fingerprint, DateTimeOffset At, SyncDirection Direction);

public sealed class SyncDirectionConverter : JsonConverter<SyncDirection>
{
    private const string SwitchToPcText = "switch->pc";
    private const string PcToSwitchText = "pc->switch";

    public override SyncDirection Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        return reader.GetString() switch
        {
            SwitchToPcText => SyncDirection.SwitchToPc,
            PcToSwitchText => SyncDirection.PcToSwitch,
            var other => throw new JsonException($"Unknown sync direction: {other}"),
        };
    }

    public override void Write(Utf8JsonWriter writer, SyncDirection value, JsonSerializerOptions options)
    {
        writer.WriteStringValue(value == SyncDirection.SwitchToPc ? SwitchToPcText : PcToSwitchText);
    }
}
