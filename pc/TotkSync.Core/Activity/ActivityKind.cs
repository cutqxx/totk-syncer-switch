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
