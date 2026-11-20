using System.Text.Json.Serialization;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

public sealed record StatusResponse(string App, int Api, bool GameRunning, SaveDto Save, LastSyncDto? LastSync);

public sealed record SaveDto(
    bool Present,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Modified = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? Files = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] long? Bytes = null,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Fingerprint = null);

public sealed record LastSyncDto(string Fingerprint, string At, SyncDirection Direction);

public sealed record ErrorResponse(string Error);

public sealed record ReasonedErrorResponse(string Error, string Reason);

public sealed record PutReplacedResponse(string Result, string Backup, string Fingerprint);

public sealed record PutUnchangedResponse(string Result, string Fingerprint);

public sealed record ConflictResponse(string Error, string PcModified, string? LastSyncAt);

public sealed record AckRequest(string? Fingerprint);

public sealed record AckRecordedResponse(string Result);

[JsonSourceGenerationOptions(PropertyNamingPolicy = JsonKnownNamingPolicy.SnakeCaseLower)]
[JsonSerializable(typeof(StatusResponse))]
[JsonSerializable(typeof(ErrorResponse))]
[JsonSerializable(typeof(ReasonedErrorResponse))]
[JsonSerializable(typeof(PutReplacedResponse))]
[JsonSerializable(typeof(PutUnchangedResponse))]
[JsonSerializable(typeof(ConflictResponse))]
[JsonSerializable(typeof(AckRequest))]
[JsonSerializable(typeof(AckRecordedResponse))]
internal sealed partial class ApiJsonContext : JsonSerializerContext;
