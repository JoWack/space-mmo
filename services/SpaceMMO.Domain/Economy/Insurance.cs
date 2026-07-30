using SpaceMMO.Domain.Loss;

namespace SpaceMMO.Domain.Economy;

/// <summary>
/// Ship insurance coverage levels, per ADR-0006. Optional; <see cref="None"/> is the default.
/// </summary>
/// <remarks>Persisted on the policy row, so names are part of the schema.</remarks>
public enum InsuranceTier
{
    /// <summary>Uninsured. Total loss on destruction.</summary>
    None = 0,

    /// <summary>40% payout for a 6% premium.</summary>
    Basic = 1,

    /// <summary>60% payout for a 12% premium.</summary>
    Standard = 2,

    /// <summary>80% payout for a 20% premium. Never 100% — see ADR-0006.</summary>
    Premium = 3,
}

/// <summary>
/// The rates for one insurance tier, in basis points.
/// </summary>
/// <param name="Tier">The tier these rates belong to.</param>
/// <param name="PayoutBasisPoints">Fraction of acquisition value paid on a covered loss.</param>
/// <param name="PremiumBasisPoints">Fraction of acquisition value charged up front.</param>
public readonly record struct InsuranceTerms(
    InsuranceTier Tier,
    int PayoutBasisPoints,
    int PremiumBasisPoints)
{
    /// <summary>True if this tier pays anything at all.</summary>
    public bool CoversLoss => PayoutBasisPoints > 0;
}

/// <summary>
/// Insurance premium and payout calculation, per ADR-0006.
/// </summary>
/// <remarks>
/// <para>
/// Everything here is pegged to a hull's recorded <c>acquisition_value</c> — the actual
/// input material value if crafted, or the actual price paid if bought. <strong>Never a
/// market reference price.</strong> That single choice is what closes the insurance fraud
/// vector: the exploit exists only when payout is pegged to a reference price an efficient
/// industrialist can beat, because then <c>payout &gt; real cost</c> becomes achievable and
/// destroying your own ships turns a profit.
/// </para>
/// <para>
/// Rounding follows ADR-0005: premiums round up (charged to the player), payouts round
/// down (paid to the player). Rounding is therefore always a sink of at most one minor
/// unit, never a faucet.
/// </para>
/// </remarks>
public static class Insurance
{
    private static readonly InsuranceTerms[] TermsByTier =
    [
        new(InsuranceTier.None, PayoutBasisPoints: 0, PremiumBasisPoints: 0),
        new(InsuranceTier.Basic, PayoutBasisPoints: 4_000, PremiumBasisPoints: 600),
        new(InsuranceTier.Standard, PayoutBasisPoints: 6_000, PremiumBasisPoints: 1_200),
        new(InsuranceTier.Premium, PayoutBasisPoints: 8_000, PremiumBasisPoints: 2_000),
    ];

    /// <summary>Rates for a tier.</summary>
    /// <exception cref="ArgumentOutOfRangeException">If the tier is not a defined value.</exception>
    public static InsuranceTerms TermsFor(InsuranceTier tier)
    {
        int index = (int)tier;

        if (index < 0 || index >= TermsByTier.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(tier), tier, "Unknown insurance tier.");
        }

        return TermsByTier[index];
    }

    /// <summary>
    /// The up-front, non-refundable premium for insuring a hull at the given tier.
    /// </summary>
    /// <param name="tier">Coverage level.</param>
    /// <param name="acquisitionValue">The hull's recorded acquisition value.</param>
    /// <exception cref="ArgumentOutOfRangeException">If the value is negative.</exception>
    public static Credits PremiumFor(InsuranceTier tier, Credits acquisitionValue)
    {
        GuardValue(acquisitionValue);

        // Rounds up: this is charged to the player.
        return acquisitionValue.PercentRoundedUp(TermsFor(tier).PremiumBasisPoints);
    }

    /// <summary>
    /// The payout for a destroyed hull, or zero if the loss is not covered.
    /// </summary>
    /// <remarks>
    /// Returns zero for <see cref="DeathCause.SelfDestruct"/>: deliberate destruction voids
    /// the policy, which removes the zero-risk path to a payout.
    /// </remarks>
    /// <param name="tier">The policy's coverage level.</param>
    /// <param name="acquisitionValue">The hull's recorded acquisition value.</param>
    /// <param name="cause">How the hull was lost.</param>
    /// <exception cref="ArgumentOutOfRangeException">If the value is negative.</exception>
    public static Credits PayoutFor(InsuranceTier tier, Credits acquisitionValue, DeathCause cause)
    {
        GuardValue(acquisitionValue);

        if (cause == DeathCause.SelfDestruct)
        {
            return Credits.Zero;
        }

        // Rounds down: this is paid to the player.
        return acquisitionValue.PercentRoundedDown(TermsFor(tier).PayoutBasisPoints);
    }

    /// <summary>
    /// The loss probability, in basis points, above which a tier pays for itself —
    /// <c>premium ÷ payout</c>.
    /// </summary>
    /// <remarks>
    /// 15% for Basic, 20% for Standard, 25% for Premium. Higher tiers demand a higher
    /// expected loss rate to be worth buying, which is the intended shape: premium coverage
    /// is a decision you make before something dangerous, not a default.
    /// <para>
    /// This is also the break-even point for the economy as a whole — above it, insurance
    /// runs as a net credit faucet. See ADR-0006 on adverse selection.
    /// </para>
    /// </remarks>
    /// <returns>Zero for <see cref="InsuranceTier.None"/>, which can never pay for itself.</returns>
    public static int BreakEvenLossRateBasisPoints(InsuranceTier tier)
    {
        InsuranceTerms terms = TermsFor(tier);

        if (!terms.CoversLoss)
        {
            return 0;
        }

        return terms.PremiumBasisPoints * Credits.OneHundredPercentBasisPoints
            / terms.PayoutBasisPoints;
    }

    /// <summary>
    /// True if losing an insured hull leaves the owner worse off than never having built it.
    /// </summary>
    /// <remarks>
    /// This must hold for every tier and every value — it is the property that makes
    /// deliberate destruction unprofitable, and a single counterexample is an exploitable
    /// credit printer. It holds structurally because payout is strictly below 100% and the
    /// premium is non-refundable:
    /// <c>payout − value − premium = −value·(1 − payoutRate) − premium &lt; 0</c>.
    /// Exposed so tests and EconSim can assert it rather than trust it.
    /// </remarks>
    public static bool LossIsAlwaysUnprofitable(InsuranceTier tier, Credits acquisitionValue)
    {
        GuardValue(acquisitionValue);

        if (acquisitionValue.IsZero)
        {
            // A worthless hull pays nothing and costs nothing; there is no profit to make.
            return PayoutFor(tier, acquisitionValue, DeathCause.ShipExplosion).IsZero;
        }

        Credits premium = PremiumFor(tier, acquisitionValue);
        Credits payout = PayoutFor(tier, acquisitionValue, DeathCause.ShipExplosion);

        return payout < acquisitionValue + premium;
    }

    private static void GuardValue(Credits acquisitionValue)
    {
        if (acquisitionValue.IsNegative)
        {
            throw new ArgumentOutOfRangeException(
                nameof(acquisitionValue), acquisitionValue, "Acquisition value cannot be negative.");
        }
    }
}
