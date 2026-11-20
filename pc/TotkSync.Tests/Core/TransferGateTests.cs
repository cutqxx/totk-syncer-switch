using TotkSync.Core.Sync;

namespace TotkSync.Tests.Core;

public sealed class TransferGateTests
{
    [Fact]
    public void Second_entry_is_refused_while_the_first_lease_is_held()
    {
        var gate = new TransferGate();

        var first = gate.TryEnter(out var lease);
        var second = gate.TryEnter(out _);

        Assert.Equal((true, false, true), (first, second, gate.IsBusy));
        lease.Dispose();
    }

    [Fact]
    public void Disposing_the_lease_frees_the_gate_for_the_next_entry()
    {
        var gate = new TransferGate();
        gate.TryEnter(out var lease);

        lease.Dispose();

        Assert.Equal((false, true), (gate.IsBusy, gate.TryEnter(out _)));
    }

    [Fact]
    public void Disposing_a_lease_twice_does_not_free_a_gate_held_by_someone_else()
    {
        var gate = new TransferGate();
        gate.TryEnter(out var first);
        first.Dispose();
        gate.TryEnter(out var second);

        first.Dispose();

        Assert.Equal((true, false), (gate.IsBusy, gate.TryEnter(out _)));
        second.Dispose();
    }

    [Fact]
    public void Refused_entry_leaves_the_gate_busy_even_when_its_empty_lease_is_disposed()
    {
        var gate = new TransferGate();
        gate.TryEnter(out var held);
        gate.TryEnter(out var refused);

        refused.Dispose();

        Assert.True(gate.IsBusy);
        held.Dispose();
    }

    [Fact]
    public void Busy_changed_reports_true_on_entry_and_false_on_release_and_nothing_for_a_refusal()
    {
        var gate = new TransferGate();
        var seen = new List<bool>();
        gate.BusyChanged += seen.Add;

        gate.TryEnter(out var lease);
        gate.TryEnter(out _);
        lease.Dispose();

        Assert.Equal([true, false], seen);
    }

    [Fact]
    public async Task Async_entry_waits_until_the_holder_releases()
    {
        var gate = new TransferGate();
        gate.TryEnter(out var holder);

        var waiting = gate.EnterAsync(CancellationToken.None);
        await Task.Delay(50);
        var wasWaiting = !waiting.IsCompleted;
        holder.Dispose();
        using var lease = await waiting.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.True(wasWaiting);
        Assert.True(gate.IsBusy);
    }

    [Fact]
    public async Task Cancelled_async_entry_does_not_leave_the_gate_busy()
    {
        var gate = new TransferGate();
        gate.TryEnter(out var holder);
        using var cancellation = new CancellationTokenSource();
        var waiting = gate.EnterAsync(cancellation.Token);

        await cancellation.CancelAsync();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => waiting);
        holder.Dispose();

        Assert.False(gate.IsBusy);
    }
}
