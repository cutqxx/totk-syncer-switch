using TotkSync.Core.Activity;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public class ActivityLogTests : IDisposable
{
    private readonly TempDir _temp = new();

    public void Dispose() => _temp.Dispose();

    private string LogPath => _temp.Combine("activity.jsonl");

    private static ActivityEntry Entry(int n, ActivityKind kind = ActivityKind.StatusOk)
    {
        return new ActivityEntry(DateTimeOffset.UnixEpoch.AddSeconds(n), "192.168.1.50", "GET", $"/p{n}", 200, kind);
    }

    [Fact]
    public void Entries_are_kept_in_the_order_they_were_added()
    {
        var log = new ActivityLog(LogPath);

        log.Append(Entry(1));
        log.Append(Entry(2));
        log.Append(Entry(3));

        Assert.Equal(["/p1", "/p2", "/p3"], log.Snapshot().Select(entry => entry.Path));
    }

    [Fact]
    public void Default_capacity_keeps_exactly_the_latest_500_in_memory_and_on_disk()
    {
        var log = new ActivityLog(LogPath);

        for (var n = 1; n <= 505; n++)
        {
            log.Append(Entry(n));
        }

        var snapshot = log.Snapshot();
        Assert.Equal(("/p6", "/p505", 500), (snapshot[0].Path, snapshot[^1].Path, snapshot.Count));
        Assert.Equal(500, File.ReadAllLines(LogPath).Length);
    }

    [Fact]
    public void Exactly_capacity_entries_evicts_nothing()
    {
        var log = new ActivityLog(LogPath, capacity: 3);

        for (var n = 1; n <= 3; n++)
        {
            log.Append(Entry(n));
        }

        Assert.Equal(["/p1", "/p2", "/p3"], log.Snapshot().Select(entry => entry.Path));
    }

    [Fact]
    public void One_entry_over_capacity_evicts_only_the_oldest()
    {
        var log = new ActivityLog(LogPath, capacity: 3);

        for (var n = 1; n <= 4; n++)
        {
            log.Append(Entry(n));
        }

        Assert.Equal(["/p2", "/p3", "/p4"], log.Snapshot().Select(entry => entry.Path));
    }

    [Fact]
    public async Task Entries_survive_restart_with_all_fields_and_order()
    {
        var first = new ActivityLog(LogPath);
        first.Append(Entry(1, ActivityKind.Unauthorized));
        first.Append(Entry(2, ActivityKind.MethodNotAllowed));

        var second = new ActivityLog(LogPath);
        var result = await second.LoadAsync();

        Assert.Equal(new LoadResult.Loaded(2), result);
        Assert.Equal(first.Snapshot(), second.Snapshot());
    }

    [Fact]
    public async Task Loading_a_file_longer_than_capacity_keeps_the_newest_entries()
    {
        var writer = new ActivityLog(LogPath, capacity: 10);
        for (var n = 1; n <= 5; n++)
        {
            writer.Append(Entry(n));
        }

        var reader = new ActivityLog(LogPath, capacity: 3);
        await reader.LoadAsync();

        Assert.Equal(["/p3", "/p4", "/p5"], reader.Snapshot().Select(entry => entry.Path));
    }

    [Fact]
    public async Task Missing_file_loads_as_empty_history()
    {
        var log = new ActivityLog(LogPath);

        var result = await log.LoadAsync();

        Assert.Equal((new LoadResult.Loaded(0), 0), (result, log.Snapshot().Count));
    }

    [Fact]
    public async Task Unreadable_file_reports_unreadable_and_the_log_keeps_working()
    {
        File.WriteAllText(LogPath, "{ this is not an entry\n");
        var log = new ActivityLog(LogPath);

        var result = await log.LoadAsync();
        log.Append(Entry(1));

        Assert.IsType<LoadResult.Unreadable>(result);
        Assert.Equal(["/p1"], log.Snapshot().Select(entry => entry.Path));
    }

    [Fact]
    public async Task Unreadable_file_is_not_overwritten_by_the_load_itself()
    {
        File.WriteAllText(LogPath, "garbage\n");

        await new ActivityLog(LogPath).LoadAsync();

        Assert.Equal("garbage\n", File.ReadAllText(LogPath));
    }

    [Fact]
    public async Task Entry_appended_while_history_is_loading_does_not_erase_the_history()
    {
        var old = new ActivityLog(LogPath);
        old.Append(Entry(1));
        old.Append(Entry(2));
        var log = new ActivityLog(LogPath);

        var loading = log.LoadAsync();
        log.Append(Entry(3));
        await loading;

        Assert.Equal(["/p1", "/p2", "/p3"], log.Snapshot().Select(entry => entry.Path));
        Assert.Equal(3, File.ReadAllLines(LogPath).Length);
    }

    [Fact]
    public void Append_without_a_load_in_progress_persists_immediately()
    {
        var log = new ActivityLog(LogPath);

        log.Append(Entry(1));

        Assert.Single(File.ReadAllLines(LogPath));
    }

    [Fact]
    public void Stored_file_has_one_json_line_per_entry_and_no_temporary_file_is_left()
    {
        var log = new ActivityLog(LogPath);
        log.Append(Entry(1));
        log.Append(Entry(2));

        Assert.Equal(2, File.ReadAllLines(LogPath).Length);
        Assert.Equal(["activity.jsonl"], Directory.GetFiles(_temp.Path).Select(Path.GetFileName));
    }

    [Fact]
    public void Stored_file_has_no_token_authorization_or_body_fields()
    {
        var log = new ActivityLog(LogPath);
        log.Append(Entry(1));

        var text = File.ReadAllText(LogPath).ToLowerInvariant();

        Assert.DoesNotContain("token", text);
        Assert.DoesNotContain("authorization", text);
        Assert.DoesNotContain("body", text);
    }

    [Fact]
    public void Write_failure_does_not_throw_and_the_entry_stays_in_memory()
    {
        Directory.CreateDirectory(LogPath);
        var log = new ActivityLog(LogPath);

        log.Append(Entry(1));

        Assert.Equal(["/p1"], log.Snapshot().Select(entry => entry.Path));
    }

    [Fact]
    public void Entry_added_event_is_raised_with_the_new_entry()
    {
        var log = new ActivityLog(LogPath);
        var seen = new List<ActivityEntry>();
        log.EntryAdded += seen.Add;

        log.Append(Entry(7));

        Assert.Equal([Entry(7)], seen);
    }

    [Fact]
    public async Task Concurrent_appends_lose_nothing()
    {
        var log = new ActivityLog(LogPath, capacity: 100);

        await Task.WhenAll(Enumerable.Range(0, 40).Select(n => Task.Run(() => log.Append(Entry(n)))));

        Assert.Equal(40, log.Snapshot().Select(entry => entry.Path).Distinct().Count());
        Assert.Equal(40, File.ReadAllLines(LogPath).Length);
    }
}
