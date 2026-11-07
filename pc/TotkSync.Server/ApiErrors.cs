using Microsoft.AspNetCore.Http;

namespace TotkSync.Server;

internal static class ApiErrors
{
    public const string Unauthorized = "unauthorized";
    public const string Forbidden = "forbidden";
    public const string NotFound = "not_found";
    public const string MethodNotAllowed = "method_not_allowed";
    public const string Internal = "internal";

    public static Task WriteAsync(HttpContext context, int statusCode, string errorCode)
    {
        context.Response.StatusCode = statusCode;
        return context.Response.WriteAsJsonAsync(new ErrorResponse(errorCode), ApiJsonContext.Default.ErrorResponse);
    }
}
