using System.Text;
using TotkSync.Core.Activity;
using TotkSync.Core.Status;

namespace TotkSync.Server;

public sealed record ApiDeps(string Token, StatusQuery Status, ActivityLog Activity, TimeProvider Time)
{
    private bool PrintMembers(StringBuilder builder)
    {
        builder.Append($"Token = ***, Status = {Status}, Activity = {Activity}, Time = {Time}");
        return true;
    }
}
