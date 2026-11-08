using Microsoft.AspNetCore.Http;
using TotkSync.Core.Activity;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

internal static class ReceiveResponses
{
    public static Task WriteAsync(HttpContext context, ReceiveResult result)
    {
        return result switch
        {
            ReceiveResult.Replaced replaced => WriteReplaced(context, replaced),
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
