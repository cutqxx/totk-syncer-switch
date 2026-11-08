using Microsoft.AspNetCore.Http;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

internal static class SaveEndpoint
{
    private static readonly TimeSpan BodyReadTimeout = TimeSpan.FromMinutes(2);

    public static async Task PutAsync(HttpContext context, SaveReceiver receiver)
    {
        var request = new PushRequest(context.Request.ContentLength, token => ReadBodyAsync(context.Request, token));
        var result = await receiver.ReceiveAsync(request, context.RequestAborted);
        await ReceiveResponses.WriteAsync(context, result);
    }

    private static async Task<byte[]> ReadBodyAsync(HttpRequest request, CancellationToken ct)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(BodyReadTimeout);
        var body = new byte[request.ContentLength!.Value];
        await request.Body.ReadExactlyAsync(body, timeout.Token);
        return body;
    }
}
