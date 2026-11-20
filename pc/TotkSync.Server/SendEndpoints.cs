using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Http;
using TotkSync.Core.Activity;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

/// <summary>GET /save hands the PC save to the Switch; POST /save/ack records that the Switch restored it.</summary>
internal static partial class SendEndpoints
{
    public const string FingerprintHeader = "X-TotkSync-Fingerprint";
    public const string ModifiedHeader = "X-TotkSync-Modified";
    private const string ZipContentType = "application/zip";
    private const int MaxAckBytes = 1024;
    private static readonly TimeSpan AckReadTimeout = TimeSpan.FromSeconds(10);

    public static async Task GetAsync(HttpContext context, SaveSender sender, TimeProvider time)
    {
        var result = await Task.Run(sender.Send, CancellationToken.None);
        switch (result)
        {
            case SendResult.Sent sent:
                await WriteZipAsync(context, sent, time);
                break;
            case SendResult.NoSave:
                await Refuse(context, StatusCodes.Status404NotFound, ActivityKind.NoSave, ApiErrors.NoSave);
                break;
            case SendResult.GameRunning:
                await Refuse(context, StatusCodes.Status423Locked, ActivityKind.GameRunning, ApiErrors.GameRunning);
                break;
            case SendResult.Busy:
                await Refuse(context, StatusCodes.Status503ServiceUnavailable, ActivityKind.Busy, ApiErrors.Busy);
                break;
            case SendResult.Failed failed:
                ActivityRecording.SetKind(context, ActivityKind.ReadFailed);
                await ApiErrors.WriteWithReasonAsync(context, StatusCodes.Status500InternalServerError, ApiErrors.ReadFailed, failed.Reason);
                break;
            default:
                throw new InvalidOperationException($"Unknown send result {result}");
        }
    }

    public static async Task AckAsync(HttpContext context, SaveSender sender)
    {
        var fingerprint = await TryReadFingerprintAsync(context.Request, context.RequestAborted);
        if (fingerprint is null)
        {
            await Refuse(context, StatusCodes.Status400BadRequest, ActivityKind.BadRequest, ApiErrors.BadRequest);
            return;
        }
        var result = await sender.AckAsync(fingerprint);
        await (result switch
        {
            AckResult.Recorded => WriteRecorded(context),
            AckResult.Mismatch => Refuse(context, StatusCodes.Status409Conflict, ActivityKind.FingerprintMismatch, ApiErrors.FingerprintMismatch),
            AckResult.NoSave => Refuse(context, StatusCodes.Status404NotFound, ActivityKind.NoSave, ApiErrors.NoSave),
            AckResult.GameRunning => Refuse(context, StatusCodes.Status423Locked, ActivityKind.GameRunning, ApiErrors.GameRunning),
            AckResult.Busy => Refuse(context, StatusCodes.Status503ServiceUnavailable, ActivityKind.Busy, ApiErrors.Busy),
            AckResult.Failed failed => RefuseWithReason(context, ActivityKind.WriteFailed, ApiErrors.WriteFailed, failed.Reason),
            _ => throw new InvalidOperationException($"Unknown ack result {result}"),
        });
    }

    internal static bool IsFingerprint(string? text) => text is not null && FingerprintPattern().IsMatch(text);

    private static async Task WriteZipAsync(HttpContext context, SendResult.Sent sent, TimeProvider time)
    {
        ActivityRecording.SetKind(context, ActivityKind.SaveSent);
        var response = context.Response;
        response.ContentType = ZipContentType;
        response.ContentLength = sent.Zip.Length;
        response.Headers[FingerprintHeader] = sent.Fingerprint;
        response.Headers[ModifiedHeader] = StatusMapping.FormatDate(sent.Modified, time);
        await response.Body.WriteAsync(sent.Zip, context.RequestAborted);
    }

    private static Task WriteRecorded(HttpContext context)
    {
        ActivityRecording.SetKind(context, ActivityKind.SyncConfirmed);
        return context.Response.WriteAsJsonAsync(new AckRecordedResponse("recorded"), ApiJsonContext.Default.AckRecordedResponse);
    }

    private static async Task<string?> TryReadFingerprintAsync(HttpRequest request, CancellationToken ct)
    {
        if (request.ContentLength is not { } length || length <= 0 || length > MaxAckBytes)
        {
            return null;
        }
        try
        {
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
            timeout.CancelAfter(AckReadTimeout);
            var body = new byte[length];
            await request.Body.ReadExactlyAsync(body, timeout.Token);
            var ack = JsonSerializer.Deserialize(body, ApiJsonContext.Default.AckRequest);
            return IsFingerprint(ack?.Fingerprint) ? ack!.Fingerprint : null;
        }
        catch (Exception error) when (error is JsonException or IOException or OperationCanceledException)
        {
            return null;
        }
    }

    private static Task Refuse(HttpContext context, int statusCode, ActivityKind kind, string errorCode)
    {
        ActivityRecording.SetKind(context, kind);
        return ApiErrors.WriteAsync(context, statusCode, errorCode);
    }

    private static Task RefuseWithReason(HttpContext context, ActivityKind kind, string errorCode, string reason)
    {
        ActivityRecording.SetKind(context, kind);
        return ApiErrors.WriteWithReasonAsync(context, StatusCodes.Status500InternalServerError, errorCode, reason);
    }

    [GeneratedRegex("^sha256:[0-9a-f]{64}$")]
    private static partial Regex FingerprintPattern();
}
