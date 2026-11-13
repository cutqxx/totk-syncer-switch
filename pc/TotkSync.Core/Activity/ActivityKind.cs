namespace TotkSync.Core.Activity;

public enum ActivityKind
{
    StatusOk,
    Unauthorized,
    Forbidden,
    NotFound,
    MethodNotAllowed,
    Internal,
    SaveReplaced,
    SaveUnchanged,
    InvalidZip,
    NoSave,
    Conflict,
    LengthRequired,
    TooLarge,
    GameRunning,
    Busy,
    WriteFailed,
    Other,
}
