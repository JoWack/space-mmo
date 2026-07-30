using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;

namespace SpaceMMO.Data.Entities;

/// <summary>
/// A limit order on one station's book.
/// </summary>
/// <remarks>
/// There is no global market — books are per-station, so regional price differences exist and
/// hauling between them is a profession (economy-design §5).
/// </remarks>
public class MarketOrder
{
    public long Id { get; set; }

    public int StationId { get; set; }

    public Station? Station { get; set; }

    /// <summary>
    /// Denormalised from the station so book queries can be scoped by system without a join.
    /// </summary>
    public int StarSystemId { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    public OrderSide Side { get; set; }

    /// <summary>Unit price. Matching is price-time priority: best price first, oldest first at ties.</summary>
    public Credits Price { get; set; }

    public int QuantityOriginal { get; set; }

    /// <summary>
    /// Decremented as fills occur. Reaching zero closes the order.
    /// </summary>
    /// <remarks>
    /// Every fill takes <c>SELECT … FOR UPDATE</c> on this row. Partial fills and two buyers
    /// hitting one sell order concurrently are the two bugs that dupe items or money.
    /// </remarks>
    public int QuantityRemaining { get; set; }

    /// <summary>
    /// Credits still held against this order. Always zero for sell orders.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Buy orders lock credits at placement, so an order can never fail to honour itself. The
    /// money leaves the buyer's balance immediately and lives here until it is paid to a seller,
    /// destroyed as tax, or released back on cancellation. Total money supply is therefore
    /// <c>Σ balances + Σ escrow on open orders</c>.
    /// </para>
    /// <para>
    /// Held here rather than in a single balance column because escrow is locked at the buyer's
    /// <em>limit</em> price while fills execute at the <em>resting</em> price. The per-order
    /// figure is what makes the price-improvement refund computable.
    /// </para>
    /// </remarks>
    public Credits EscrowedCredits { get; set; }

    /// <summary>
    /// Units of the item held against this order. Always zero for buy orders.
    /// </summary>
    /// <remarks>
    /// The sell-side mirror of escrow: goods leave the seller's inventory at placement, so a sell
    /// order cannot promise items the seller has since spent elsewhere.
    /// </remarks>
    public int ReservedQuantity { get; set; }

    /// <summary>
    /// Cost basis travelling with <see cref="ReservedQuantity"/>. Zero for buy orders.
    /// </summary>
    /// <remarks>
    /// Held so that cancelling a sell order returns the goods at what they originally cost, rather
    /// than resetting them to free. Without it, listing and cancelling would launder away an
    /// item's cost basis and inflate the insurance value of anything later built from it.
    /// </remarks>
    public Credits ReservedCostBasis { get; set; }

    public DateTimeOffset PlacedAt { get; set; }

    /// <summary>
    /// Mandatory, so abandoned orders self-clean rather than accumulating forever as stale
    /// prices that mislead every player reading the book.
    /// </summary>
    public DateTimeOffset ExpiresAt { get; set; }

    public DateTimeOffset? CancelledAt { get; set; }
}

/// <summary>
/// An executed trade. Append-only.
/// </summary>
/// <remarks>
/// Both the audit trail and the price-history source for market UI, so it is never pruned —
/// only archived.
/// </remarks>
public class Trade
{
    public long Id { get; set; }

    public long BuyOrderId { get; set; }

    public long SellOrderId { get; set; }

    public int StationId { get; set; }

    public int StarSystemId { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    public int BuyerCharacterId { get; set; }

    public int SellerCharacterId { get; set; }

    public int Quantity { get; set; }

    /// <summary>Unit price the trade executed at.</summary>
    public Credits Price { get; set; }

    /// <summary>Tax taken from the seller's proceeds. The main volumetric credit sink.</summary>
    public Credits SalesTax { get; set; }

    public DateTimeOffset ExecutedAt { get; set; }
}

/// <summary>A time-gated manufacturing job.</summary>
public class IndustryJob
{
    public long Id { get; set; }

    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    public int RecipeId { get; set; }

    public Recipe? Recipe { get; set; }

    public int StationId { get; set; }

    public Station? Station { get; set; }

    /// <summary>How many times the recipe runs. Inputs scale with this.</summary>
    public int Runs { get; set; }

    public IndustryJobState State { get; set; }

    public DateTimeOffset StartedAt { get; set; }

    /// <summary>
    /// When outputs become claimable. Computed server-side from the recipe's duration; the
    /// client's clock is never consulted.
    /// </summary>
    public DateTimeOffset CompletesAt { get; set; }

    public DateTimeOffset? ClaimedAt { get; set; }

    /// <summary>Fee charged at job start. Ties the sink to real production volume.</summary>
    public Credits FeePaid { get; set; }

    /// <summary>
    /// Total cost basis of everything consumed, carried into the output's acquisition value.
    /// </summary>
    public Credits InputCostBasis { get; set; }

    /// <summary>
    /// Exactly what this job consumed, recorded at start.
    /// </summary>
    /// <remarks>
    /// Stored rather than re-derived from the recipe, because recipes are content and can be
    /// rebalanced. A job started before a change must refund what it actually took, not what the
    /// recipe says today.
    /// </remarks>
    public ICollection<IndustryJobInput> Inputs { get; } = [];
}

/// <summary>One material a job consumed, with what it cost.</summary>
public class IndustryJobInput
{
    public long IndustryJobId { get; set; }

    public IndustryJob? IndustryJob { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    /// <summary>Units taken, already multiplied by the run count.</summary>
    public int Quantity { get; set; }

    /// <summary>What those units cost their owner, for the refund and the output's value.</summary>
    public Credits CostBasis { get; set; }
}
