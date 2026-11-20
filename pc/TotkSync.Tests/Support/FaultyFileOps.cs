using TotkSync.Core.Replace;

namespace TotkSync.Tests.Support;

public enum FileOp
{
    CreateDirectory,
    WriteFile,
    CopyFile,
    MoveDirectory,
    MoveFile,
    DeleteDirectory,
    DeleteFile,
}

public enum FaultTiming
{
    BeforeOperation,
    AfterOperation,
}

public sealed record FileCall(FileOp Op, string Path, string? Target = null);

/// <summary>Wraps the real file operations and fails chosen calls with an IOException.</summary>
public sealed class FaultyFileOps(IFileOps inner) : IFileOps
{
    private readonly Dictionary<FileOp, int> _counts = [];
    private readonly List<FileCall> _calls = [];

    public FaultTiming Timing { get; set; } = FaultTiming.BeforeOperation;

    public Func<FileCall, int, bool>? FailWhen { get; set; }

    public Action<FileCall>? OnBefore { get; set; }

    public Action<FileCall>? OnAfter { get; set; }

    public Func<string, Exception>? CreateFailure { get; set; }

    public bool HasFailed { get; private set; }

    public IReadOnlyList<FileCall> Calls => _calls;

    public int CountOf(FileOp op) => _counts.GetValueOrDefault(op);

    public FaultyFileOps FailOnNth(FileOp op, int nth, FaultTiming timing = FaultTiming.BeforeOperation)
    {
        Timing = timing;
        FailWhen = (call, callNumber) => call.Op == op && callNumber == nth;
        return this;
    }

    public FaultyFileOps FailFromNth(FileOp op, int nth, FaultTiming timing = FaultTiming.BeforeOperation)
    {
        Timing = timing;
        FailWhen = (call, callNumber) => call.Op == op && callNumber >= nth;
        return this;
    }

    public FaultyFileOps FailWhenPath(FileOp op, Func<string, bool> pathPredicate)
    {
        FailWhen = (call, _) => call.Op == op && pathPredicate(call.Path);
        return this;
    }

    public void CreateDirectory(string path)
    {
        var call = new FileCall(FileOp.CreateDirectory, path);
        var fault = Begin(call);
        inner.CreateDirectory(path);
        End(call, fault);
    }

    public void WriteFile(string path, ReadOnlySpan<byte> content)
    {
        var call = new FileCall(FileOp.WriteFile, path);
        var fault = Begin(call);
        inner.WriteFile(path, content);
        End(call, fault);
    }

    public void CopyFile(string from, string to)
    {
        var call = new FileCall(FileOp.CopyFile, from, to);
        var fault = Begin(call);
        inner.CopyFile(from, to);
        End(call, fault);
    }

    public void MoveDirectory(string from, string to)
    {
        var call = new FileCall(FileOp.MoveDirectory, from, to);
        var fault = Begin(call);
        inner.MoveDirectory(from, to);
        End(call, fault);
    }

    public void MoveFile(string from, string to, bool overwrite)
    {
        var call = new FileCall(FileOp.MoveFile, from, to);
        var fault = Begin(call);
        inner.MoveFile(from, to, overwrite);
        End(call, fault);
    }

    public void DeleteDirectory(string path)
    {
        var call = new FileCall(FileOp.DeleteDirectory, path);
        var fault = Begin(call);
        inner.DeleteDirectory(path);
        End(call, fault);
    }

    public void DeleteFile(string path)
    {
        var call = new FileCall(FileOp.DeleteFile, path);
        var fault = Begin(call);
        inner.DeleteFile(path);
        End(call, fault);
    }

    private bool Begin(FileCall call)
    {
        var number = _counts.GetValueOrDefault(call.Op) + 1;
        _counts[call.Op] = number;
        _calls.Add(call);
        OnBefore?.Invoke(call);
        var shouldFail = FailWhen?.Invoke(call, number) == true;
        if (shouldFail && Timing == FaultTiming.BeforeOperation)
        {
            Fail(call);
        }
        return shouldFail;
    }

    private void End(FileCall call, bool shouldFail)
    {
        OnAfter?.Invoke(call);
        if (shouldFail)
        {
            Fail(call);
        }
    }

    private void Fail(FileCall call)
    {
        HasFailed = true;
        var message = $"injected failure: {call.Op} {call.Path}";
        throw CreateFailure?.Invoke(message) ?? new IOException(message);
    }
}
