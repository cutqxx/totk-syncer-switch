using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace TotkSync.Server;

internal static class ApiEndpoints
{
    private sealed record Handler(string Method, Func<HttpContext, Task> Handle);

    public static void Map(IEndpointRouteBuilder routes, ApiDeps deps)
    {
        routes.Map("/status", context => Dispatch(context, new Handler(HttpMethods.Get, ctx => WriteStatus(ctx, deps))));
        var saveHandlers = SaveHandlers(deps);
        if (saveHandlers.Length > 0)
        {
            routes.Map("/save", context => Dispatch(context, saveHandlers));
        }
        if (deps.Sender is { } sender)
        {
            routes.Map("/save/ack", context => Dispatch(context, new Handler(HttpMethods.Post, ctx => SendEndpoints.AckAsync(ctx, sender))));
        }
        routes.MapFallback("{*path}", context => ApiErrors.WriteAsync(context, StatusCodes.Status404NotFound, ApiErrors.NotFound));
    }

    private static Handler[] SaveHandlers(ApiDeps deps)
    {
        var handlers = new List<Handler>();
        if (deps.Sender is { } sender)
        {
            handlers.Add(new Handler(HttpMethods.Get, ctx => SendEndpoints.GetAsync(ctx, sender, deps.Time)));
        }
        if (deps.Receiver is { } receiver)
        {
            handlers.Add(new Handler(HttpMethods.Put, ctx => SaveEndpoint.PutAsync(ctx, receiver, deps.Time)));
        }
        return handlers.ToArray();
    }

    private static Task Dispatch(HttpContext context, params Handler[] handlers)
    {
        var handler = handlers.FirstOrDefault(candidate => HttpMethods.Equals(context.Request.Method, candidate.Method));
        if (handler is not null)
        {
            return handler.Handle(context);
        }
        context.Response.Headers.Allow = string.Join(", ", handlers.Select(candidate => candidate.Method));
        return ApiErrors.WriteAsync(context, StatusCodes.Status405MethodNotAllowed, ApiErrors.MethodNotAllowed);
    }

    private static Task WriteStatus(HttpContext context, ApiDeps deps)
    {
        var response = StatusMapping.ToResponse(deps.Status.Get(), deps.Time);
        return context.Response.WriteAsJsonAsync(response, ApiJsonContext.Default.StatusResponse);
    }
}
