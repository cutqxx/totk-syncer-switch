using System.Text;
using TotkSync.Core.Activity;
using TotkSync.Core.Status;
using TotkSync.Core.Sync;

namespace TotkSync.Server;

public sealed record ApiDeps(string Token, StatusQuery Status, ActivityLog Activity, TimeProvider Time, SaveReceiver? Receiver = null, Action<Exception>? OnError = null, SaveSender? Sender = null)
{
    private bool PrintMembers(StringBuilder builder)
    {
        builder.Append($"Token = ***, Status = {Status}, Activity = {Activity}, Time = {Time}");
        return true;
    }
}
