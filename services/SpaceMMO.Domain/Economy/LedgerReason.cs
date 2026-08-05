namespace SpaceMMO.Domain.Economy;

/// <summary>
/// Why credits moved. Required on every ledger entry, per ADR-0005.
/// </summary>
/// <remarks>
/// <para>
/// This enum is what makes the EconSim invariants checkable: faucet and sink attribution
/// is a <c>GROUP BY</c> over the ledger, so every credit created or destroyed has to be
/// explainable by exactly one of these.
/// </para>
/// <para>
/// The categorisation below matters more than the individual values. <em>Faucets</em>
/// create credits, <em>sinks</em> destroy them, and <em>transfers</em> move them between
/// characters without changing the money supply. Confusing a transfer for a faucet is how
/// an economy quietly inflates.
/// </para>
/// </remarks>
public enum LedgerReason
{
    // ── Faucets: these create credits ────────────────────────────────────────

    /// <summary>
    /// A repeatable sidequest reward — the steady-state faucet, and the only one subject to the
    /// daily cap (economy-design §2b).
    /// </summary>
    QuestReward = 0,

    /// <summary>
    /// A one-shot story or career reward — the bootstrap faucet.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Deliberately <em>not</em> capped. The onboarding chain pays 13,000 credits, so a 5,000
    /// daily cap would throttle a new player's tutorial across three days and make the game look
    /// broken at the worst possible moment.
    /// </para>
    /// <para>
    /// Safe to leave uncapped because it is bounded by construction: each quest completes once per
    /// character, so the total is fixed no matter how much anyone plays. Alt-account farming is
    /// the only attack and it pays terribly per hour.
    /// </para>
    /// <para>
    /// Separating it from <see cref="QuestReward"/> also lets EconSim measure the bootstrap and
    /// steady-state faucets independently, which are genuinely different things — one scales with
    /// signups, the other with active play.
    /// </para>
    /// </remarks>
    StoryReward = 3,

    /// <summary>
    /// An insurance payout. Deliberately exempt from the daily faucet cap, and bounded by
    /// production rather than by a budget (ADR-0006).
    /// </summary>
    InsurancePayout = 1,

    /// <summary>
    /// A manual correction by an operator. Should be vanishingly rare, and every occurrence
    /// is worth investigating.
    /// </summary>
    AdminAdjustment = 2,

    /// <summary>
    /// The stake a character is created with.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Exists because every industry job charges a fee and a new character had nothing to pay it
    /// with. The onboarding questline was meant to cover that, and does — but only by ordering, and
    /// only for a player who actually follows it. Anyone who skipped the tutorial could gather ore
    /// forever and never craft.
    /// </para>
    /// <para>
    /// Its own reason rather than folded into <see cref="StoryReward"/> so EconSim can measure it
    /// separately. It scales with signups rather than with play, which is a different shape of
    /// faucet, and a bootstrap number hidden inside the quest total would be invisible exactly when
    /// someone is trying to work out where the money came from.
    /// </para>
    /// </remarks>
    StartingStake = 4,

    /// <summary>
    /// A faction standing order buying raw material off a player — the faucet of last resort.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Exists so that no character can ever be stuck. At zero credits a player cannot start a job,
    /// because jobs charge a fee, and could not even list ore for sale, because placing an order
    /// charges a broker fee up front. Gathering was the only action left and it produced no money.
    /// This is the standing bid that turns gathered material into credits with nothing paid first.
    /// </para>
    /// <para>
    /// <strong>Capped, and sharing the same daily budget as sidequests.</strong> It is farmable by
    /// construction — ore regrows — so leaving it uncapped would make it an income limited only by
    /// how fast someone can mine. Routing it through the existing budget is what
    /// <see cref="FaucetBudget"/> was built for, and means adding it costs no rebalancing.
    /// </para>
    /// <para>
    /// It is also a material sink: what the faction buys leaves the economy. That matters as much
    /// as the credits, because raw material otherwise only ever accumulates.
    /// </para>
    /// </remarks>
    FactionPurchase = 5,

    // ── Sinks: these destroy credits ─────────────────────────────────────────

    /// <summary>Charged on placing a market order. Discourages order spam.</summary>
    BrokerFee = 10,

    /// <summary>Charged on a filled trade. The main volumetric sink.</summary>
    SalesTax = 11,

    /// <summary>Charged on starting an industry job. Ties the sink to production volume.</summary>
    IndustryFee = 12,

    /// <summary>Recurring charge for station storage. Punishes infinite hoarding.</summary>
    StationRent = 13,

    /// <summary>Fuel consumed by warp and sublight burn. Scales with trade activity.</summary>
    FuelPurchase = 14,

    /// <summary>An insurance premium. Partially offsets payouts.</summary>
    InsurancePremium = 15,

    /// <summary>Materials and credits spent restoring a damaged item.</summary>
    RepairCost = 16,

    // ── Transfers: these move credits without changing the supply ────────────

    /// <summary>
    /// Credits locked out of a buyer's balance when they place a buy order.
    /// </summary>
    /// <remarks>
    /// A transfer, not a sink: the money still exists, it just lives on the order rather than in
    /// the balance until the order fills or is cancelled. Total money supply is therefore
    /// <c>Σ balances + Σ open order escrow</c>.
    /// </remarks>
    MarketEscrowLocked = 20,

    /// <summary>
    /// Escrow returned to a buyer — on cancellation, expiry, or as a price-improvement refund
    /// when a fill executed below their limit price.
    /// </summary>
    MarketEscrowReleased = 21,

    /// <summary>Received by the seller in a market trade, net of sales tax.</summary>
    MarketSale = 22,

    /// <summary>Escrowed when posting a bounty on another player.</summary>
    BountyPosted = 23,

    /// <summary>Paid out to whoever collected a bounty.</summary>
    BountyClaimed = 24,

    /// <summary>A direct player-to-player transfer.</summary>
    PlayerTransfer = 25,
}

/// <summary>
/// Classifies a <see cref="LedgerReason"/> by its effect on the money supply.
/// </summary>
public enum LedgerReasonKind
{
    /// <summary>Creates credits that did not previously exist.</summary>
    Faucet = 0,

    /// <summary>Destroys credits permanently.</summary>
    Sink = 1,

    /// <summary>Moves credits between characters; supply unchanged.</summary>
    Transfer = 2,
}

/// <summary>
/// Money-supply classification for ledger reasons.
/// </summary>
public static class LedgerReasons
{
    /// <summary>
    /// Whether a reason creates, destroys, or merely moves credits.
    /// </summary>
    /// <remarks>
    /// Deliberately a total switch with no default arm: adding a new
    /// <see cref="LedgerReason"/> without classifying it becomes a compile error rather
    /// than an unattributed credit flow that quietly breaks the EconSim invariants.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If the reason is not a defined value.</exception>
    public static LedgerReasonKind KindOf(LedgerReason reason) => reason switch
    {
        LedgerReason.QuestReward or
        LedgerReason.StoryReward or
        LedgerReason.StartingStake or
        LedgerReason.FactionPurchase or
        LedgerReason.InsurancePayout or
        LedgerReason.AdminAdjustment => LedgerReasonKind.Faucet,

        LedgerReason.BrokerFee or
        LedgerReason.SalesTax or
        LedgerReason.IndustryFee or
        LedgerReason.StationRent or
        LedgerReason.FuelPurchase or
        LedgerReason.InsurancePremium or
        LedgerReason.RepairCost => LedgerReasonKind.Sink,

        LedgerReason.MarketEscrowLocked or
        LedgerReason.MarketEscrowReleased or
        LedgerReason.MarketSale or
        LedgerReason.BountyPosted or
        LedgerReason.BountyClaimed or
        LedgerReason.PlayerTransfer => LedgerReasonKind.Transfer,

        _ => throw new ArgumentOutOfRangeException(nameof(reason), reason, "Unclassified ledger reason."),
    };

    /// <summary>
    /// True if this reason is subject to the daily faucet cap.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The capped faucets are the farmable ones: repeatable sidequests
    /// (<see cref="LedgerReason.QuestReward"/>) and faction standing orders
    /// (<see cref="LedgerReason.FactionPurchase"/>), which a player can feed indefinitely because
    /// deposits respawn. They share one budget rather than getting one each, so a character's total
    /// daily intake is bounded no matter which they use — splitting the budget would let someone
    /// take both in full and double the rate the economy was balanced for.
    /// </para>
    /// <para>
    /// The rest are each bounded by something other than a budget:
    /// <see cref="LedgerReason.StoryReward"/> by each quest completing once per character,
    /// <see cref="LedgerReason.StartingStake"/> by being paid once at creation,
    /// <see cref="LedgerReason.InsurancePayout"/> by how many ships players can actually build, and
    /// <see cref="LedgerReason.AdminAdjustment"/> by an operator deliberately doing it.
    /// </para>
    /// </remarks>
    public static bool IsCappedFaucet(LedgerReason reason) =>
        reason is LedgerReason.QuestReward or LedgerReason.FactionPurchase;
}

/// <summary>Which side of the order book an order sits on.</summary>
public enum OrderSide
{
    /// <summary>A bid: the character wants to buy at or below their price.</summary>
    Buy = 0,

    /// <summary>An ask: the character wants to sell at or above their price.</summary>
    Sell = 1,
}
