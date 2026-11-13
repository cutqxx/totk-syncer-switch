using Microsoft.AspNetCore.Http;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

internal static class SaveEndpoint
{
    private const string ForceValue = "1";
    private static readonly TimeSpan BodyReadTimeout = TimeSpan.FromMinutes(2);

    public static async Task PutAsync(HttpContext context, SaveReceiver receiver, TimeProvider time)
    {
        var request = new PushRequest(
            context.Request.ContentLength,
            token => ReadBodyAsync(context.Request, token),
            IsForced(context.Request));
        var result = await receiver.ReceiveAsync(request, context.RequestAborted);
        await ReceiveResponses.WriteAsync(context, result, time);
    }

    private static bool IsForced(HttpRequest request) => request.Query["force"] == ForceValue;

    private static async Task<byte[]> ReadBodyAsync(HttpRequest request, CancellationToken ct)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(BodyReadTimeout);
        var body = new byte[request.ContentLength!.Value];
        await request.Body.ReadExactlyAsync(body, timeout.Token);
        return body;
    }
}
