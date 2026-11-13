using Microsoft.AspNetCore.Http;

namespace TotkSync.Server;

internal static class ApiErrors
{
    public const string Unauthorized = "unauthorized";
    public const string Forbidden = "forbidden";
    public const string NotFound = "not_found";
    public const string MethodNotAllowed = "method_not_allowed";
    public const string Internal = "internal";
    public const string InvalidZip = "invalid_zip";
    public const string NoSave = "no_save";
    public const string LengthRequired = "length_required";
    public const string TooLarge = "too_large";
    public const string GameRunning = "game_running";
    public const string Busy = "busy";
    public const string WriteFailed = "write_failed";
    public const string Conflict = "conflict";

    public static Task WriteAsync(HttpContext context, int statusCode, string errorCode)
    {
        context.Response.StatusCode = statusCode;
        return context.Response.WriteAsJsonAsync(new ErrorResponse(errorCode), ApiJsonContext.Default.ErrorResponse);
    }

    public static Task WriteWithReasonAsync(HttpContext context, int statusCode, string errorCode, string reason)
    {
        context.Response.StatusCode = statusCode;
        return context.Response.WriteAsJsonAsync(new ReasonedErrorResponse(errorCode, reason), ApiJsonContext.Default.ReasonedErrorResponse);
    }
}
