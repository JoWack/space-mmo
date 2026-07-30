using SpaceMMO.Domain.Economy;
using Xunit;

namespace SpaceMMO.Domain.Tests.Economy;

/// <summary>
/// Tests for the credit faucet chokepoint (economy-design §2b).
/// </summary>
/// <remarks>
/// The cap is what makes adding future faucets safe without rebalancing, so the invariant
/// that matters most is simply that no sequence of grants can exceed it.
/// </remarks>
public sealed class FaucetBudgetTests
{
    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    [Fact]
    public void DefaultDailyCap_IsFiveThousandCredits()
    {
        Assert.Equal(Cr(5_000), FaucetBudget.DefaultDailyCap);
    }

    [Fact]
    public void Evaluate_WellUnderTheCap_GrantsInFull()
    {
        FaucetGrant grant = FaucetBudget.Evaluate(Cr(500), Cr(1_000), Cr(5_000));

        Assert.Equal(Cr(500), grant.Granted);
        Assert.True(grant.Withheld.IsZero);
        Assert.False(grant.WasCapped);
    }

    [Fact]
    public void Evaluate_ExactlyReachingTheCap_GrantsInFull()
    {
        FaucetGrant grant = FaucetBudget.Evaluate(Cr(1_000), Cr(4_000), Cr(5_000));

        Assert.Equal(Cr(1_000), grant.Granted);
        Assert.False(grant.WasCapped);
    }

    [Fact]
    public void Evaluate_StraddlingTheCap_GrantsThePartialRemainder()
    {
        // The interesting case: a 2,000 cr reward with only 500 cr of budget left.
        FaucetGrant grant = FaucetBudget.Evaluate(Cr(2_000), Cr(4_500), Cr(5_000));

        Assert.Equal(Cr(500), grant.Granted);
        Assert.Equal(Cr(1_500), grant.Withheld);
        Assert.True(grant.WasCapped);
    }

    [Fact]
    public void Evaluate_AtTheCap_GrantsNothing()
    {
        FaucetGrant grant = FaucetBudget.Evaluate(Cr(1_000), Cr(5_000), Cr(5_000));

        Assert.True(grant.Granted.IsZero);
        Assert.Equal(Cr(1_000), grant.Withheld);
        Assert.True(grant.WasCapped);
    }

    [Fact]
    public void Evaluate_AlreadyOverTheCap_GrantsNothingRatherThanGoingNegative()
    {
        // Lowering the cap in config can leave characters above the new limit. The
        // remaining budget must clamp at zero, not wrap into a negative that would then
        // be subtracted into a windfall.
        FaucetGrant grant = FaucetBudget.Evaluate(Cr(1_000), Cr(9_000), Cr(5_000));

        Assert.True(grant.Granted.IsZero);
        Assert.Equal(Cr(1_000), grant.Withheld);
    }

    [Fact]
    public void Evaluate_WithAZeroCap_GrantsNothing()
    {
        FaucetGrant grant = FaucetBudget.Evaluate(Cr(1_000), Credits.Zero, Credits.Zero);

        Assert.True(grant.Granted.IsZero);
        Assert.Equal(Cr(1_000), grant.Withheld);
    }

    [Fact]
    public void Evaluate_RequestingNothing_GrantsNothingAndIsNotCapped()
    {
        FaucetGrant grant = FaucetBudget.Evaluate(Credits.Zero, Cr(5_000), Cr(5_000));

        Assert.True(grant.Granted.IsZero);
        Assert.False(grant.WasCapped);
    }

    [Fact]
    public void Evaluate_AlwaysAccountsForTheFullRequest()
    {
        // Granted + Withheld must equal Requested, or the ledger cannot be reconciled
        // against what sources believed they awarded.
        foreach (long requested in new[] { 0L, 1L, 499L, 5_000L, 100_000L })
        {
            foreach (long already in new[] { 0L, 1L, 2_500L, 5_000L, 9_999L })
            {
                FaucetGrant grant = FaucetBudget.Evaluate(Cr(requested), Cr(already), Cr(5_000));

                Assert.Equal(Cr(requested), grant.Requested);
                Assert.Equal(Cr(requested), grant.Granted + grant.Withheld);
            }
        }
    }

    [Fact]
    public void Evaluate_NoSequenceOfGrantsCanExceedTheCap()
    {
        // The load-bearing invariant. Simulates a character grinding repeatable quests all
        // day: however many grants are made, the total is bounded.
        Credits cap = Cr(5_000);
        Credits running = Credits.Zero;

        for (int i = 0; i < 200; i++)
        {
            FaucetGrant grant = FaucetBudget.Evaluate(Cr(750), running, cap);
            running += grant.Granted;

            Assert.True(running <= cap, $"Total granted {running} exceeded cap {cap} at grant {i}.");
        }

        // And it should actually reach the cap rather than stalling early.
        Assert.Equal(cap, running);
    }

    [Fact]
    public void Evaluate_DefaultOverload_UsesTheDefaultCap()
    {
        FaucetGrant explicitCap =
            FaucetBudget.Evaluate(Cr(9_000), Credits.Zero, FaucetBudget.DefaultDailyCap);
        FaucetGrant defaulted = FaucetBudget.Evaluate(Cr(9_000), Credits.Zero);

        Assert.Equal(explicitCap, defaulted);
        Assert.Equal(Cr(5_000), defaulted.Granted);
    }

    [Theory]
    [InlineData(-1, 0, 5_000)]
    [InlineData(0, -1, 5_000)]
    [InlineData(0, 0, -1)]
    public void Evaluate_WithNegativeArguments_Throws(long requested, long already, long cap)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => FaucetBudget.Evaluate(
            Credits.FromMinorUnits(requested),
            Credits.FromMinorUnits(already),
            Credits.FromMinorUnits(cap)));
    }
}
