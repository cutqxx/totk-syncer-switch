using Microsoft.AspNetCore.Http;
using TotkSync.Core.Activity;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

internal static class ReceiveResponses
{
    public static Task WriteAsync(HttpContext context, ReceiveResult result, TimeProvider time)
    {
        return result switch
        {
            ReceiveResult.Replaced replaced => WriteReplaced(context, replaced),
            ReceiveResult.Unchanged unchanged => WriteUnchanged(context, unchanged),
            ReceiveResult.Conflict conflict => WriteConflict(context, conflict, time),
            ReceiveResult.InvalidZip invalid => RefuseWithReason(context, StatusCodes.Status400BadRequest, ActivityKind.InvalidZip, ApiErrors.InvalidZip, invalid.Reason),
            ReceiveResult.Failed failed => RefuseWithReason(context, StatusCodes.Status500InternalServerError, ActivityKind.WriteFailed, ApiErrors.WriteFailed, failed.Reason),
            ReceiveResult.NoSave => Refuse(context, StatusCodes.Status404NotFound, ActivityKind.NoSave, ApiErrors.NoSave),
            ReceiveResult.LengthRequired => Refuse(context, StatusCodes.Status411LengthRequired, ActivityKind.LengthRequired, ApiErrors.LengthRequired),
            ReceiveResult.TooLarge => Refuse(context, StatusCodes.Status413PayloadTooLarge, ActivityKind.TooLarge, ApiErrors.TooLarge),
            ReceiveResult.GameRunning => Refuse(context, StatusCodes.Status423Locked, ActivityKind.GameRunning, ApiErrors.GameRunning),
            ReceiveResult.Busy => Refuse(context, StatusCodes.Status503ServiceUnavailable, ActivityKind.Busy, ApiErrors.Busy),
            _ => throw new InvalidOperationException($"Unknown receive result {result}"),
        };
    }

    private static Task WriteReplaced(HttpContext context, ReceiveResult.Replaced replaced)
    {
        ActivityRecording.SetKind(context, ActivityKind.SaveReplaced);
        var body = new PutReplacedResponse("replaced", replaced.BackupName, replaced.Fingerprint);
        return context.Response.WriteAsJsonAsync(body, ApiJsonContext.Default.PutReplacedResponse);
    }

    private static Task WriteUnchanged(HttpContext context, ReceiveResult.Unchanged unchanged)
    {
        ActivityRecording.SetKind(context, ActivityKind.SaveUnchanged);
        var body = new PutUnchangedResponse("unchanged", unchanged.Fingerprint);
        return context.Response.WriteAsJsonAsync(body, ApiJsonContext.Default.PutUnchangedResponse);
    }

    private static Task WriteConflict(HttpContext context, ReceiveResult.Conflict conflict, TimeProvider time)
    {
        ActivityRecording.SetKind(context, ActivityKind.Conflict);
        context.Response.StatusCode = StatusCodes.Status409Conflict;
        var body = new ConflictResponse(
            ApiErrors.Conflict,
            StatusMapping.FormatDate(conflict.PcModified, time),
            conflict.LastSyncAt is { } lastSyncAt ? StatusMapping.FormatDate(lastSyncAt, time) : null);
        return context.Response.WriteAsJsonAsync(body, ApiJsonContext.Default.ConflictResponse);
    }

    private static Task Refuse(HttpContext context, int statusCode, ActivityKind kind, string errorCode)
    {
        ActivityRecording.SetKind(context, kind);
        return ApiErrors.WriteAsync(context, statusCode, errorCode);
    }

    private static Task RefuseWithReason(HttpContext context, int statusCode, ActivityKind kind, string errorCode, string reason)
    {
        ActivityRecording.SetKind(context, kind);
        return ApiErrors.WriteWithReasonAsync(context, statusCode, errorCode, reason);
    }
}
