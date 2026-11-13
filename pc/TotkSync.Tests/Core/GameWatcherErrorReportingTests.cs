using Microsoft.Extensions.Time.Testing;
using TotkSync.Core.Game;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class GameWatcherErrorReportingTests : IDisposable
{
    private readonly TempDir _temp = new();
    private readonly TotkSync.Core.Sync.TransferGate _gate = new();
    private readonly List<string> _errors = [];

    public void Dispose() => _temp.Dispose();

    private sealed class ScriptedGame(params string?[] script) : IGameProcess
    {
        private int _calls;

        public int Calls => Volatile.Read(ref _calls);

        public bool IsRunning()
        {
            var step = script[Math.Min(Interlocked.Increment(ref _calls), script.Length) - 1];
            return step is null ? false : throw new InvalidOperationException(step);
        }
    }

    private async Task RunPollsAsync(ScriptedGame game, int polls)
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: true);
        var time = new FakeTimeProvider();
        using var watcher = new GameWatcher(game, _gate, rig.Cache, time, error => _errors.Add(error.Message));
        var loop = watcher.Start();
        for (var poll = 1; poll <= polls; poll++)
        {
            await WaitForCallsAsync(game, poll);
            time.Advance(GameWatcher.PollInterval);
        }
        await WaitForCallsAsync(game, polls + 1);
        watcher.Dispose();
        await loop;
    }

    private static async Task WaitForCallsAsync(ScriptedGame game, int calls)
    {
        var deadline = DateTime.UtcNow.AddSeconds(10);
        while (game.Calls < calls && DateTime.UtcNow < deadline)
        {
            await Task.Delay(10);
        }
        Assert.True(game.Calls >= calls, "poll did not happen in time");
        await Task.Delay(30);
    }

    [Fact]
    public async Task Repeated_identical_failures_are_reported_once()
    {
        await RunPollsAsync(new ScriptedGame("probe failed", "probe failed", "probe failed"), polls: 2);

        Assert.Equal(["probe failed"], _errors);
    }

    [Fact]
    public async Task A_different_failure_message_is_reported_again()
    {
        await RunPollsAsync(new ScriptedGame("first", "first", "second"), polls: 2);

        Assert.Equal(["first", "second"], _errors);
    }

    [Fact]
    public async Task A_successful_tick_resets_deduplication_so_the_same_failure_is_reported_again()
    {
        await RunPollsAsync(new ScriptedGame("flaky", null, "flaky"), polls: 2);

        Assert.Equal(["flaky", "flaky"], _errors);
    }

    [Fact]
    public async Task Cancellation_while_waiting_for_the_gate_is_not_reported_as_an_error()
    {
        var rig = await StatusRig.CreateAsync(_temp.Path, withSave: true);
        var time = new FakeTimeProvider();
        using var cts = new CancellationTokenSource();
        var watcher = new GameWatcher(rig.Game, _gate, rig.Cache, time, error => _errors.Add(error.Message));
        rig.Game.Running = true;
        var loop = watcher.RunAsync(cts.Token);
        await Task.Delay(100);
        _gate.TryEnter(out var write);
        rig.Game.Running = false;
        time.Advance(GameWatcher.PollInterval);
        await Task.Delay(100);

        cts.Cancel();
        await loop;
        write.Dispose();

        Assert.Empty(_errors);
    }
}
