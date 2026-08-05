using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Domain.Market;

/// <summary>
/// The credits a market transaction costs, per economy-design §3.
/// </summary>
/// <remarks>
/// <para>
/// Both fees are proportional rather than flat, on purpose: a flat fee becomes irrelevant as
/// the economy grows, whereas a percentage keeps scaling with wealth and remains a meaningful
/// sink for as long as the game runs.
/// </para>
/// <para>
/// Every fee rounds <em>up</em> (ADR-0005). A fee that rounds to zero on small transactions
/// is a free option, and free options get farmed.
/// </para>
/// </remarks>
public static class MarketFees
{
    /// <summary>
    /// Charged on placing an order, whether or not it ever fills. First draft: 1%.
    /// </summary>
    /// <remarks>
    /// Non-refundable on cancellation, which is what makes it discourage order spam. Placing
    /// and cancelling to probe the book has to cost something, or the book becomes noise.
    /// </remarks>
    public const int DefaultBrokerFeeBasisPoints = 100;

    /// <summary>
    /// Taken from the seller's proceeds on each fill. First draft: 2%.
    /// </summary>
    /// <remarks>
    /// The main volumetric credit sink, and the primary lever for balancing the economy
    /// against the daily faucet cap. Expected to move once EconSim can measure the sink rate.
    /// </remarks>
    public const int DefaultSalesTaxBasisPoints = 200;

    /// <summary>Total value of a quantity at a unit price.</summary>
    /// <exception cref="ArgumentOutOfRangeException">If quantity is not positive.</exception>
    public static Credits GrossValue(Credits unitPrice, int quantity)
    {
        if (quantity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(quantity), quantity, "Quantity must be positive.");
        }

        return unitPrice * quantity;
    }

    /// <summary>
    /// Broker fee charged to whoever places an order.
    /// </summary>
    /// <param name="unitPrice">Order's unit price.</param>
    /// <param name="quantity">Order's full quantity.</param>
    /// <param name="basisPoints">Rate; defaults to <see cref="DefaultBrokerFeeBasisPoints"/>.</param>
    public static Credits BrokerFee(
        Credits unitPrice, int quantity, int basisPoints = DefaultBrokerFeeBasisPoints) =>
        GrossValue(unitPrice, quantity).PercentRoundedUp(basisPoints);

    /// <summary>
    /// The largest broker fee that may ever be waived. First draft: 1 credit.
    /// </summary>
    /// <remarks>
    /// <para>
    /// This ceiling is the whole reason the waiver is safe, and it is not obvious. The broker fee is
    /// a percentage of order value, so a bigger order owes a bigger fee — which means a rule of the
    /// form "waive it when the seller cannot afford it" is <em>easier</em> to trigger the more
    /// valuable the order. A player could spend down to nothing, list a fortune, and dodge the
    /// entire fee. The rule would scale exactly backwards from the intent.
    /// </para>
    /// <para>
    /// Capping the waivable amount removes that completely: the most anyone can ever avoid is this
    /// number. At the default 1% rate it covers orders up to 100 credits, which is enough to get a
    /// broke player back into the market and far too little to be worth arranging.
    /// </para>
    /// </remarks>
    public static Credits MaxWaivedBrokerFee { get; } = Credits.FromWholeCredits(1);

    /// <summary>
    /// The broker fee actually charged, after the lockout waiver.
    /// </summary>
    /// <param name="grossFee">What the fee would be at full rate.</param>
    /// <param name="balance">What the seller currently holds.</param>
    /// <param name="side">Which side of the book the order sits on.</param>
    /// <remarks>
    /// <para>
    /// <strong>Nobody may be locked out of converting goods into money.</strong> Placing a sell
    /// order charges its fee up front, so a seller with an empty balance and a full hangar could not
    /// place one — the only two ways out of zero both wanted money first.
    /// </para>
    /// <para>
    /// Three conditions, all required. It applies to <em>sells</em> only, because a buyer with no
    /// credits is not locked out of anything: they have nothing to convert, and escrow is money
    /// held rather than money spent. It applies only when the seller genuinely cannot pay, so it is
    /// never a discount for the solvent. And it applies only below
    /// <see cref="MaxWaivedBrokerFee"/>, which is what stops it becoming a way to list a fortune for
    /// free.
    /// </para>
    /// <para>
    /// A player whose order is too large to qualify is not stuck: they can list a smaller parcel,
    /// take the proceeds, and pay full rate on the rest.
    /// </para>
    /// </remarks>
    public static Credits EffectiveBrokerFee(Credits grossFee, Credits balance, OrderSide side)
    {
        if (side != OrderSide.Sell)
        {
            return grossFee;
        }

        if (balance >= grossFee)
        {
            return grossFee;
        }

        return grossFee > MaxWaivedBrokerFee ? grossFee : Credits.Zero;
    }

    /// <summary>
    /// Sales tax deducted from a seller's proceeds on a fill.
    /// </summary>
    /// <param name="unitPrice">Execution price.</param>
    /// <param name="quantity">Units filled.</param>
    /// <param name="basisPoints">Rate; defaults to <see cref="DefaultSalesTaxBasisPoints"/>.</param>
    public static Credits SalesTax(
        Credits unitPrice, int quantity, int basisPoints = DefaultSalesTaxBasisPoints) =>
        GrossValue(unitPrice, quantity).PercentRoundedUp(basisPoints);

    /// <summary>
    /// What a seller actually receives after tax.
    /// </summary>
    /// <remarks>
    /// Since tax rounds up and is subtracted, the seller absorbs the rounding — correct, as
    /// they are the party being charged.
    /// </remarks>
    public static Credits NetSellerProceeds(
        Credits unitPrice, int quantity, int basisPoints = DefaultSalesTaxBasisPoints) =>
        GrossValue(unitPrice, quantity) - SalesTax(unitPrice, quantity, basisPoints);

    /// <summary>
    /// What a buyer pays. Buyers pay gross — the tax comes out of the seller's side, so a
    /// single trade is never taxed twice.
    /// </summary>
    public static Credits BuyerCost(Credits unitPrice, int quantity) => GrossValue(unitPrice, quantity);
}
