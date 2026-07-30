using SpaceMMO.Domain.Economy;
using Xunit;

namespace SpaceMMO.Domain.Tests.Economy;

/// <summary>
/// Tests for ledger reason classification (ADR-0005, economy-design §4).
/// </summary>
/// <remarks>
/// Faucet and sink attribution is a <c>GROUP BY</c> over <see cref="LedgerReason"/>, so an
/// unclassified or mis-classified reason silently breaks the EconSim invariants. Mistaking a
/// transfer for a faucet is specifically how an economy inflates without anyone noticing.
/// </remarks>
public sealed class LedgerReasonTests
{
    private static readonly LedgerReason[] AllReasons = Enum.GetValues<LedgerReason>();

    [Fact]
    public void EveryReason_IsClassified()
    {
        // The test that matters most: adding a reason without classifying it must not slip
        // through into production as an unattributed credit flow.
        foreach (LedgerReason reason in AllReasons)
        {
            LedgerReasonKind kind = LedgerReasons.KindOf(reason);

            Assert.InRange(kind, LedgerReasonKind.Faucet, LedgerReasonKind.Transfer);
        }
    }

    [Theory]
    [InlineData(LedgerReason.QuestReward, LedgerReasonKind.Faucet)]
    [InlineData(LedgerReason.InsurancePayout, LedgerReasonKind.Faucet)]
    [InlineData(LedgerReason.AdminAdjustment, LedgerReasonKind.Faucet)]
    [InlineData(LedgerReason.BrokerFee, LedgerReasonKind.Sink)]
    [InlineData(LedgerReason.SalesTax, LedgerReasonKind.Sink)]
    [InlineData(LedgerReason.IndustryFee, LedgerReasonKind.Sink)]
    [InlineData(LedgerReason.StationRent, LedgerReasonKind.Sink)]
    [InlineData(LedgerReason.FuelPurchase, LedgerReasonKind.Sink)]
    [InlineData(LedgerReason.InsurancePremium, LedgerReasonKind.Sink)]
    [InlineData(LedgerReason.RepairCost, LedgerReasonKind.Sink)]
    [InlineData(LedgerReason.MarketEscrowLocked, LedgerReasonKind.Transfer)]
    [InlineData(LedgerReason.MarketEscrowReleased, LedgerReasonKind.Transfer)]
    [InlineData(LedgerReason.MarketSale, LedgerReasonKind.Transfer)]
    [InlineData(LedgerReason.BountyPosted, LedgerReasonKind.Transfer)]
    [InlineData(LedgerReason.BountyClaimed, LedgerReasonKind.Transfer)]
    [InlineData(LedgerReason.PlayerTransfer, LedgerReasonKind.Transfer)]
    public void KindOf_ClassifiesEachReasonCorrectly(LedgerReason reason, LedgerReasonKind expected)
    {
        Assert.Equal(expected, LedgerReasons.KindOf(reason));
    }

    [Fact]
    public void OnlyQuestRewards_AreSubjectToTheDailyCap()
    {
        // Insurance payouts are exempt on purpose — losing a capital ship must not be
        // throttled by a daily budget (ADR-0006) — and admin adjustments bypass the cap
        // because bypassing it is the point of them.
        foreach (LedgerReason reason in AllReasons)
        {
            bool expected = reason == LedgerReason.QuestReward;

            Assert.Equal(expected, LedgerReasons.IsCappedFaucet(reason));
        }
    }

    [Fact]
    public void EveryCappedReason_IsAFaucet()
    {
        // Capping a sink or a transfer would be meaningless, and would suggest the cap had
        // been wired to the wrong thing.
        foreach (LedgerReason reason in AllReasons.Where(LedgerReasons.IsCappedFaucet))
        {
            Assert.Equal(LedgerReasonKind.Faucet, LedgerReasons.KindOf(reason));
        }
    }

    [Fact]
    public void AllThreeKinds_AreRepresented()
    {
        // A game economy missing any of the three would be badly broken: no faucet means
        // deflation to zero, no sink means runaway inflation.
        var kinds = AllReasons.Select(LedgerReasons.KindOf).Distinct().ToList();

        Assert.Equal(3, kinds.Count);
    }

    [Fact]
    public void UnclassifiedReason_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => LedgerReasons.KindOf((LedgerReason)999));
    }
}
