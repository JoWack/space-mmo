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
