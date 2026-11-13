using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace TotkSync.Server;

internal static class ApiEndpoints
{
    public static void Map(IEndpointRouteBuilder routes, ApiDeps deps)
    {
        routes.Map("/status", context => AllowOnly(HttpMethods.Get, context, () => WriteStatus(context, deps)));
        if (deps.Receiver is { } receiver)
        {
            routes.Map("/save", context => AllowOnly(HttpMethods.Put, context, () => SaveEndpoint.PutAsync(context, receiver, deps.Time)));
        }
        routes.MapFallback("{*path}", context => ApiErrors.WriteAsync(context, StatusCodes.Status404NotFound, ApiErrors.NotFound));
    }

    private static Task AllowOnly(string method, HttpContext context, Func<Task> handle)
    {
        if (HttpMethods.Equals(context.Request.Method, method))
        {
            return handle();
        }
        context.Response.Headers.Allow = method;
        return ApiErrors.WriteAsync(context, StatusCodes.Status405MethodNotAllowed, ApiErrors.MethodNotAllowed);
    }

    private static Task WriteStatus(HttpContext context, ApiDeps deps)
    {
        var response = StatusMapping.ToResponse(deps.Status.Get(), deps.Time);
        return context.Response.WriteAsJsonAsync(response, ApiJsonContext.Default.StatusResponse);
    }
}
