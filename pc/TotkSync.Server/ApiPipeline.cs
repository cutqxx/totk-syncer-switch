using System.Net;
using System.Text;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;

namespace TotkSync.Server;

internal static class ApiPipeline
{
    public const string MarkerHeader = "X-TotkSync-Api";
    public const string MarkerValue = "1";

    public static void Configure(WebApplication app, ApiDeps deps)
    {
        UseGuards(app, deps);
        ApiEndpoints.Map(app, deps);
    }

    internal static void UseGuards(IApplicationBuilder app, ApiDeps deps)
    {
        app.Use(AddMarkerHeader);
        app.Use(ActivityRecording.Middleware(deps.Activity, deps.Time));
        app.Use(AnswerInternalErrors);
        app.Use(RejectUnlessAllowed(Encoding.UTF8.GetBytes(deps.Token)));
    }

    private static async Task AnswerInternalErrors(HttpContext context, RequestDelegate next)
    {
        try
        {
            await next(context);
        }
        catch (Exception) when (!context.Response.HasStarted)
        {
            context.Response.Headers[MarkerHeader] = MarkerValue;
            await ApiErrors.WriteAsync(context, StatusCodes.Status500InternalServerError, ApiErrors.Internal);
        }
    }

    private static Task AddMarkerHeader(HttpContext context, RequestDelegate next)
    {
        context.Response.Headers[MarkerHeader] = MarkerValue;
        return next(context);
    }

    internal static Func<HttpContext, RequestDelegate, Task> RejectUnlessAllowed(byte[] token)
    {
        return (context, next) =>
        {
            var remote = context.Connection.RemoteIpAddress ?? IPAddress.None;
            var verdict = RequestGuard.Check(remote, context.Request.Headers.Authorization.ToString(), token);
            return verdict switch
            {
                GuardResult.Forbidden => ApiErrors.WriteAsync(context, StatusCodes.Status403Forbidden, ApiErrors.Forbidden),
                GuardResult.Unauthorized => ApiErrors.WriteAsync(context, StatusCodes.Status401Unauthorized, ApiErrors.Unauthorized),
                _ => next(context),
            };
        };
    }
}
