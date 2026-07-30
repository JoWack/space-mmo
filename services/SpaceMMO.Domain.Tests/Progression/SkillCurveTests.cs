using SpaceMMO.Domain.Progression;
using Xunit;

namespace SpaceMMO.Domain.Tests.Progression;

/// <summary>
/// Tests for the XP curve (ADR-0004).
/// </summary>
/// <remarks>
/// The expected values below are the published RuneScape thresholds. They are the
/// contract: the curve is permanent once players have invested time in it, so any
/// change to these numbers is a change to every existing character's level and must
/// never happen accidentally. Treat a failure here as a regression, never as a test
/// that needs updating.
/// </remarks>
public sealed class SkillCurveTests
{
    [Theory]
    [InlineData(1, 0L)]
    [InlineData(2, 83L)]
    [InlineData(3, 174L)]
    [InlineData(4, 276L)]
    [InlineData(5, 388L)]
    [InlineData(6, 512L)]
    [InlineData(7, 650L)]
    [InlineData(8, 801L)]
    [InlineData(9, 969L)]
    [InlineData(10, 1_154L)]
    [InlineData(20, 4_470L)]
    [InlineData(30, 13_363L)]
    [InlineData(40, 37_224L)]
    [InlineData(50, 101_333L)]
    [InlineData(60, 273_742L)]
    [InlineData(70, 737_627L)]
    [InlineData(80, 1_986_068L)]
    [InlineData(85, 3_258_594L)]
    [InlineData(90, 5_346_332L)]
    [InlineData(92, 6_517_253L)]
    [InlineData(95, 8_771_558L)]
    [InlineData(98, 11_805_606L)]
    [InlineData(99, 13_034_431L)]
    public void XpForLevel_MatchesPublishedThresholds(int level, long expectedXp)
    {
        Assert.Equal(expectedXp, SkillCurve.XpForLevel(level));
    }

    [Fact]
    public void XpForLevel_AtMaxLevel_MatchesTheDeclaredConstant()
    {
        // Guards against MaxLevelXp drifting away from the computed table, which
        // would silently break every caller that uses the constant as a shortcut.
        Assert.Equal(SkillCurve.MaxLevelXp, SkillCurve.XpForLevel(SkillCurve.MaxLevel));
    }

    [Fact]
    public void XpForLevel_IsStrictlyIncreasing()
    {
        // A flat or decreasing step would make LevelForXp ambiguous and could let a
        // character gain two levels from one XP point.
        for (int level = SkillCurve.MinLevel; level < SkillCurve.MaxLevel; level++)
        {
            Assert.True(
                SkillCurve.XpForLevel(level) < SkillCurve.XpForLevel(level + 1),
                $"Threshold for level {level + 1} must exceed level {level}.");
        }
    }

    [Theory]
    [InlineData(0)]
    [InlineData(100)]
    [InlineData(-1)]
    [InlineData(int.MaxValue)]
    public void XpForLevel_OutsideValidRange_Throws(int level)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => SkillCurve.XpForLevel(level));
    }

    // ── LevelForXp ───────────────────────────────────────────────────────────

    [Theory]
    [InlineData(0L, 1)]
    [InlineData(82L, 1)]      // one short of level 2
    [InlineData(83L, 2)]      // exactly on the threshold
    [InlineData(84L, 2)]
    [InlineData(1_153L, 9)]   // one short of level 10
    [InlineData(1_154L, 10)]
    [InlineData(101_332L, 49)]
    [InlineData(101_333L, 50)]
    [InlineData(13_034_430L, 98)]
    [InlineData(13_034_431L, 99)]
    public void LevelForXp_ReturnsCorrectLevelAtAndAroundThresholds(long xp, int expectedLevel)
    {
        Assert.Equal(expectedLevel, SkillCurve.LevelForXp(xp));
    }

    [Fact]
    public void LevelForXp_IsExactInverseOfXpForLevel()
    {
        // The strongest single property: every threshold must map back to its own
        // level, and one XP below it must map to the level beneath.
        for (int level = SkillCurve.MinLevel; level <= SkillCurve.MaxLevel; level++)
        {
            long threshold = SkillCurve.XpForLevel(level);

            Assert.Equal(level, SkillCurve.LevelForXp(threshold));

            if (level > SkillCurve.MinLevel)
            {
                Assert.Equal(level - 1, SkillCurve.LevelForXp(threshold - 1));
            }
        }
    }

    [Theory]
    [InlineData(13_034_432L)]
    [InlineData(50_000_000L)]
    [InlineData(long.MaxValue)]
    public void LevelForXp_AboveMaxThreshold_ClampsToMaxLevel(long xp)
    {
        // Overflow XP past 99 is tracked (players keep earning it) but grants no
        // further levels, and must never index past the end of the table.
        Assert.Equal(SkillCurve.MaxLevel, SkillCurve.LevelForXp(xp));
    }

    [Fact]
    public void LevelForXp_NegativeXp_Throws()
    {
        // Negative XP is always an upstream bug — surface it loudly rather than
        // silently reporting level 1.
        Assert.Throws<ArgumentOutOfRangeException>(() => SkillCurve.LevelForXp(-1L));
    }

    // ── XpToNextLevel ────────────────────────────────────────────────────────

    [Theory]
    [InlineData(0L, 83L)]          // level 1, needs the full first level
    [InlineData(82L, 1L)]          // one XP short of level 2
    [InlineData(83L, 91L)]         // just reached level 2; 174 - 83
    [InlineData(1_154L, 204L)]     // level 10; 1358 - 1154
    public void XpToNextLevel_ReturnsRemainingXp(long xp, long expectedRemaining)
    {
        Assert.Equal(expectedRemaining, SkillCurve.XpToNextLevel(xp));
    }

    [Theory]
    [InlineData(13_034_431L)]
    [InlineData(99_999_999L)]
    public void XpToNextLevel_AtOrAboveMaxLevel_IsZero(long xp)
    {
        Assert.Equal(0L, SkillCurve.XpToNextLevel(xp));
    }

    [Fact]
    public void XpToNextLevel_AlwaysLandsExactlyOnTheNextThreshold()
    {
        // Adding the reported remainder must produce the next level and never
        // overshoot into the level after it — the property the skills panel relies on.
        for (int level = SkillCurve.MinLevel; level < SkillCurve.MaxLevel; level++)
        {
            long xp = SkillCurve.XpForLevel(level);
            long reached = xp + SkillCurve.XpToNextLevel(xp);

            Assert.Equal(SkillCurve.XpForLevel(level + 1), reached);
            Assert.Equal(level + 1, SkillCurve.LevelForXp(reached));
        }
    }

    // ── ProgressToNextLevel ──────────────────────────────────────────────────

    [Fact]
    public void ProgressToNextLevel_AtThreshold_IsZero()
    {
        Assert.Equal(0.0, SkillCurve.ProgressToNextLevel(SkillCurve.XpForLevel(50)));
    }

    [Fact]
    public void ProgressToNextLevel_AtMaxLevel_IsOne()
    {
        Assert.Equal(1.0, SkillCurve.ProgressToNextLevel(SkillCurve.MaxLevelXp));
    }

    [Fact]
    public void ProgressToNextLevel_Midway_IsApproximatelyHalf()
    {
        long start = SkillCurve.XpForLevel(30);
        long end = SkillCurve.XpForLevel(31);
        long midpoint = start + ((end - start) / 2);

        Assert.Equal(0.5, SkillCurve.ProgressToNextLevel(midpoint), precision: 3);
    }

    [Fact]
    public void ProgressToNextLevel_StaysWithinUnitRange()
    {
        // A progress bar outside 0..1 renders as a visual glitch, so bound it
        // across every level rather than spot-checking.
        for (int level = SkillCurve.MinLevel; level < SkillCurve.MaxLevel; level++)
        {
            long start = SkillCurve.XpForLevel(level);
            long end = SkillCurve.XpForLevel(level + 1);

            foreach (long xp in new[] { start, start + 1, (start + end) / 2, end - 1 })
            {
                double progress = SkillCurve.ProgressToNextLevel(xp);

                Assert.InRange(progress, 0.0, 1.0);
            }
        }
    }

    // ── Describe ─────────────────────────────────────────────────────────────

    [Fact]
    public void Describe_AgreesWithTheIndividualAccessors()
    {
        // Describe exists purely to avoid three binary searches per skill row, so
        // its only real requirement is that it never disagrees with them.
        foreach (long xp in new[] { 0L, 82L, 83L, 1_154L, 101_333L, 6_517_253L, 13_034_430L })
        {
            SkillProgress described = SkillCurve.Describe(xp);

            Assert.Equal(SkillCurve.LevelForXp(xp), described.Level);
            Assert.Equal(xp, described.Xp);
            Assert.Equal(SkillCurve.XpToNextLevel(xp), described.XpToNextLevel);
            Assert.Equal(
                SkillCurve.ProgressToNextLevel(xp),
                described.ProgressToNextLevel,
                precision: 10);
        }
    }

    [Fact]
    public void Describe_AtMaxLevel_ReportsCompletion()
    {
        SkillProgress described = SkillCurve.Describe(SkillCurve.MaxLevelXp);

        Assert.Equal(SkillCurve.MaxLevel, described.Level);
        Assert.Equal(0L, described.XpToNextLevel);
        Assert.Equal(1.0, described.ProgressToNextLevel);
    }

    [Fact]
    public void Describe_WithOverflowXp_KeepsTheRawTotal()
    {
        // Players keep earning XP past 99; the stored total must not be truncated,
        // because leaderboards and future level-cap raises depend on it.
        const long overflow = 25_000_000L;

        SkillProgress described = SkillCurve.Describe(overflow);

        Assert.Equal(SkillCurve.MaxLevel, described.Level);
        Assert.Equal(overflow, described.Xp);
    }

    [Fact]
    public void Describe_NegativeXp_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => SkillCurve.Describe(-1L));
    }

    // ── Design intent ────────────────────────────────────────────────────────

    [Fact]
    public void Level92_IsRoughlyHalfwayToLevel99InXpTerms()
    {
        // A well-known property of this curve and a deliberate part of why it was
        // adopted: late progression has a legible structure. If this ever breaks,
        // the curve has been altered.
        double ratio = (double)SkillCurve.XpForLevel(92) / SkillCurve.XpForLevel(99);

        Assert.InRange(ratio, 0.49, 0.51);
    }
}
