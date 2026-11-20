using System.Globalization;
using TotkSync.Core.Backups;

namespace TotkSync.Tests.Core;

public sealed class BackupNameTests
{
    private static readonly DateTimeOffset Noon = new(2026, 11, 7, 12, 5, 9, TimeSpan.FromHours(3));

    [Fact]
    public void Name_uses_the_local_time_with_zero_padding()
    {
        Assert.Equal("pc-20261107-120509-before-switch.zip", BackupName.BeforeSwitch(Noon));
    }

    [Fact]
    public void Name_is_independent_of_the_current_culture()
    {
        var previous = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = new CultureInfo("ar-SA");

            Assert.Equal("pc-20261107-120509-before-switch.zip", BackupName.BeforeSwitch(Noon));
        }
        finally
        {
            CultureInfo.CurrentCulture = previous;
        }
    }

    [Fact]
    public void Free_name_is_the_plain_name_when_nothing_exists()
    {
        Assert.Equal("pc-20261107-120509-before-switch.zip", BackupName.FirstFreeBeforeSwitch(Noon, _ => false));
    }

    [Fact]
    public void Taken_names_move_the_time_forward_one_second_at_a_time()
    {
        var taken = new HashSet<string> { "pc-20261107-120509-before-switch.zip", "pc-20261107-120510-before-switch.zip" };

        Assert.Equal("pc-20261107-120511-before-switch.zip", BackupName.FirstFreeBeforeSwitch(Noon, taken.Contains));
    }

    [Fact]
    public void Collision_at_the_last_second_of_the_day_rolls_into_the_next_day()
    {
        var lastSecond = new DateTimeOffset(2026, 12, 31, 23, 59, 59, TimeSpan.Zero);
        var taken = new HashSet<string> { "pc-20261231-235959-before-switch.zip" };

        Assert.Equal("pc-20270101-000000-before-switch.zip", BackupName.FirstFreeBeforeSwitch(lastSecond, taken.Contains));
    }
}
