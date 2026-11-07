using System.Net;
using Microsoft.AspNetCore.Http;
using TotkSync.Core.Activity;

namespace TotkSync.Server;

internal static class ActivityRecording
{
    private const int MaxMethodLength = 16;
    private const int MaxPathLength = 200;

    public static Func<HttpContext, RequestDelegate, Task> Middleware(ActivityLog log, TimeProvider time)
    {
        return async (context, next) =>
        {
            try
            {
                await next(context);
            }
            finally
            {
                TryAppend(log, context, time);
            }
        };
    }

    private static void TryAppend(ActivityLog log, HttpContext context, TimeProvider time)
    {
        try
        {
            log.Append(EntryFor(context, time));
        }
        catch (Exception)
        {
            // Recording is best effort: it must never replace the outcome of the request being recorded.
        }
    }

    private static ActivityEntry EntryFor(HttpContext context, TimeProvider time)
    {
        return new ActivityEntry(
            time.GetUtcNow(),
            ClientAddress(context),
            Truncate(context.Request.Method, MaxMethodLength),
            Truncate(context.Request.Path.Value ?? "", MaxPathLength),
            context.Response.StatusCode,
            KindOf(context.Response.StatusCode));
    }

    private static string ClientAddress(HttpContext context)
    {
        var remote = context.Connection.RemoteIpAddress ?? IPAddress.None;
        return (remote.IsIPv4MappedToIPv6 ? remote.MapToIPv4() : remote).ToString();
    }

    private static ActivityKind KindOf(int statusCode)
    {
        return statusCode switch
        {
            StatusCodes.Status200OK => ActivityKind.StatusOk,
            StatusCodes.Status401Unauthorized => ActivityKind.Unauthorized,
            StatusCodes.Status403Forbidden => ActivityKind.Forbidden,
            StatusCodes.Status404NotFound => ActivityKind.NotFound,
            StatusCodes.Status405MethodNotAllowed => ActivityKind.MethodNotAllowed,
            StatusCodes.Status500InternalServerError => ActivityKind.Internal,
            _ => ActivityKind.Other,
        };
    }

    private static string Truncate(string value, int maxLength)
    {
        return value.Length <= maxLength ? value : value[..maxLength];
    }
}
