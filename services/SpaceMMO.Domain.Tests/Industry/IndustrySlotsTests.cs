using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Progression;
using Xunit;

namespace SpaceMMO.Domain.Tests.Industry;

/// <summary>
/// Tests for industry job slots (design-bible §6).
/// </summary>
public sealed class IndustrySlotsTests
{
    [Theory]
    [InlineData(1, 1)]
    [InlineData(24, 1)]
    [InlineData(25, 2)]
    [InlineData(49, 2)]
    [InlineData(50, 3)]
    [InlineData(74, 3)]
    [InlineData(75, 4)]
    [InlineData(98, 4)]
    [InlineData(99, 5)]
    public void MaxConcurrentJobs_GrantsASlotAtEachThreshold(int level, int expected)
    {
        Assert.Equal(expected, IndustrySlots.MaxConcurrentJobs(level));
    }

    [Fact]
    public void MaxConcurrentJobs_NeverDecreasesWithLevel()
    {
        // Losing a slot on level-up would be an obvious bug, and a stall would make a threshold
        // silently do nothing.
        int previous = 0;

        for (int level = SkillCurve.MinLevel; level <= SkillCurve.MaxLevel; level++)
        {
            int slots = IndustrySlots.MaxConcurrentJobs(level);

            Assert.True(slots >= previous, $"Slots fell at level {level}.");
            previous = slots;
        }
    }

    [Fact]
    public void MaxSlots_MatchesTheValueAtLevel99()
    {
        Assert.Equal(IndustrySlots.MaxSlots, IndustrySlots.MaxConcurrentJobs(SkillCurve.MaxLevel));
        Assert.Equal(5, IndustrySlots.MaxSlots);
    }

    [Fact]
    public void BaseSlots_IsOne_SoANewCharacterCanCraft()
    {
        // A zero would make the onboarding questline impossible to start.
        Assert.Equal(1, IndustrySlots.BaseSlots);
        Assert.Equal(1, IndustrySlots.MaxConcurrentJobs(SkillCurve.MinLevel));
    }

    [Theory]
    [InlineData(1, 25)]
    [InlineData(24, 25)]
    [InlineData(25, 50)]
    [InlineData(74, 75)]
    [InlineData(75, 99)]
    public void NextSlotLevel_PointsAtTheUpcomingThreshold(int level, int expected)
    {
        Assert.Equal(expected, IndustrySlots.NextSlotLevel(level));
    }

    [Fact]
    public void NextSlotLevel_AtMaxLevel_IsNull()
    {
        Assert.Null(IndustrySlots.NextSlotLevel(SkillCurve.MaxLevel));
    }

    [Fact]
    public void NextSlotLevel_AlwaysAgreesWithTheSlotCount()
    {
        // Reaching the advertised level must actually grant the slot.
        for (int level = SkillCurve.MinLevel; level < SkillCurve.MaxLevel; level++)
        {
            int? next = IndustrySlots.NextSlotLevel(level);

            if (next is null)
            {
                continue;
            }

            Assert.True(
                IndustrySlots.MaxConcurrentJobs(next.Value) > IndustrySlots.MaxConcurrentJobs(level),
                $"Level {next} was advertised as a slot threshold but grants nothing.");
        }
    }

    [Theory]
    [InlineData(0)]
    [InlineData(100)]
    [InlineData(-1)]
    public void MaxConcurrentJobs_OutsideTheLevelRange_Throws(int level)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => IndustrySlots.MaxConcurrentJobs(level));
    }
}
