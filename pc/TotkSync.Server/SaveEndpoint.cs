using Microsoft.AspNetCore.Http;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

internal static class SaveEndpoint
{
    private const string ForceValue = "1";
    private const int BaseTimeoutSeconds = 30;
    private const long MinSpeedBytesPerSecond = 64L * 1024;
    private const int MaxTimeoutSeconds = 900;

    public static async Task PutAsync(HttpContext context, SaveReceiver receiver, TimeProvider time)
    {
        var request = new PushRequest(
            context.Request.ContentLength,
            token => ReadBodyAsync(context.Request, token),
            IsForced(context.Request));
        var result = await receiver.ReceiveAsync(request, context.RequestAborted);
        await ReceiveResponses.WriteAsync(context, result, time);
    }

    internal static TimeSpan BodyReadTimeout(long contentLength)
    {
        var transferSeconds = contentLength / MinSpeedBytesPerSecond + (contentLength % MinSpeedBytesPerSecond == 0 ? 0 : 1);
        var seconds = BaseTimeoutSeconds + transferSeconds;
        return TimeSpan.FromSeconds(Math.Min(seconds, MaxTimeoutSeconds));
    }

    private static bool IsForced(HttpRequest request) => request.Query["force"] == ForceValue;

    private static async Task<byte[]> ReadBodyAsync(HttpRequest request, CancellationToken ct)
    {
        var length = request.ContentLength!.Value;
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(BodyReadTimeout(length));
        var body = new byte[length];
        await request.Body.ReadExactlyAsync(body, timeout.Token);
        return body;
    }
}
