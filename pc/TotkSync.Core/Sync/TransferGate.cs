namespace TotkSync.Core.Sync;

public sealed class TransferGate
{
    private readonly SemaphoreSlim _semaphore = new(1, 1);

    public bool IsBusy => _semaphore.CurrentCount == 0;

    public event Action<bool>? BusyChanged;

    public bool TryEnter(out IDisposable lease)
    {
        var isEntered = _semaphore.Wait(0);
        lease = isEntered ? new Lease(this) : EmptyLease.Instance;
        if (isEntered)
        {
            BusyChanged?.Invoke(true);
        }
        return isEntered;
    }

    public async Task<IDisposable> EnterAsync(CancellationToken ct)
    {
        await _semaphore.WaitAsync(ct);
        BusyChanged?.Invoke(true);
        return new Lease(this);
    }

    private void Release()
    {
        _semaphore.Release();
        BusyChanged?.Invoke(false);
    }

    private sealed class Lease(TransferGate gate) : IDisposable
    {
        private int _isReleased;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _isReleased, 1) == 0)
            {
                gate.Release();
            }
        }
    }

    private sealed class EmptyLease : IDisposable
    {
        public static readonly EmptyLease Instance = new();

        public void Dispose()
        {
        }
    }
}
