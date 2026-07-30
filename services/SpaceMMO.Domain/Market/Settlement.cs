using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Domain.Market;

/// <summary>
/// How the credits for one fill divide up.
/// </summary>
/// <param name="EscrowConsumed">Credits taken from the buy order's escrow.</param>
/// <param name="BuyerRefund">
/// Escrow returned to the buyer because the fill executed below their limit price.
/// </param>
/// <param name="SellerProceeds">What the seller actually receives, after tax.</param>
/// <param name="SalesTax">Credits destroyed. The main volumetric sink.</param>
public readonly record struct FillSettlement(
    Credits EscrowConsumed,
    Credits BuyerRefund,
    Credits SellerProceeds,
    Credits SalesTax)
{
    /// <summary>
    /// Total escrow released by this fill, whether it went to the seller, to tax, or back to
    /// the buyer.
    /// </summary>
    public Credits TotalEscrowReleased => EscrowConsumed + BuyerRefund;
}

/// <summary>
/// Divides the credits for a fill between seller, tax, and buyer refund.
/// </summary>
/// <remarks>
/// <para>
/// Buy orders lock credits at placement, so by the time a fill happens the money has already
/// left the buyer's balance. Settlement therefore does not debit the buyer — it decides how the
/// escrow they already paid gets distributed.
/// </para>
/// <para>
/// Pure, so the arithmetic that decides who gets paid what is testable without a database.
/// </para>
/// </remarks>
public static class Settlement
{
    /// <summary>
    /// Settles one fill.
    /// </summary>
    /// <param name="escrowUnitPrice">
    /// The rate credits were escrowed at — always the buy order's own limit price.
    /// </param>
    /// <param name="fillUnitPrice">The execution price.</param>
    /// <param name="quantity">Units filled.</param>
    /// <param name="salesTaxBasisPoints">Tax rate on the seller's proceeds.</param>
    /// <remarks>
    /// <para>
    /// The refund exists because escrow is locked at the buyer's <em>limit</em> price while
    /// fills execute at the <em>resting</em> price, which for a buying taker is at or below
    /// their limit. Without refunding the difference, a buyer who bid 150 and filled at 100
    /// would quietly lose 50 per unit — money that would vanish from the economy entirely.
    /// </para>
    /// <para>
    /// When the buyer is the resting side, the fill price equals their own price, so the
    /// refund falls out as zero. One formula covers both cases.
    /// </para>
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If quantity is not positive, either price is not positive, or the fill price exceeds the
    /// escrow price — which would mean charging the buyer more than they agreed to.
    /// </exception>
    public static FillSettlement ForFill(
        Credits escrowUnitPrice,
        Credits fillUnitPrice,
        int quantity,
        int salesTaxBasisPoints = MarketFees.DefaultSalesTaxBasisPoints)
    {
        if (quantity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(quantity), quantity, "Quantity must be positive.");
        }

        if (!escrowUnitPrice.IsPositive)
        {
            throw new ArgumentOutOfRangeException(
                nameof(escrowUnitPrice), escrowUnitPrice, "Escrow price must be positive.");
        }

        if (!fillUnitPrice.IsPositive)
        {
            throw new ArgumentOutOfRangeException(
                nameof(fillUnitPrice), fillUnitPrice, "Fill price must be positive.");
        }

        if (fillUnitPrice > escrowUnitPrice)
        {
            throw new ArgumentOutOfRangeException(
                nameof(fillUnitPrice),
                fillUnitPrice,
                $"Fill price {fillUnitPrice} exceeds the escrowed price {escrowUnitPrice}; a buyer "
                + "must never be charged above their limit.");
        }

        Credits escrowConsumed = fillUnitPrice * quantity;
        Credits refund = (escrowUnitPrice - fillUnitPrice) * quantity;
        Credits tax = MarketFees.SalesTax(fillUnitPrice, quantity, salesTaxBasisPoints);

        return new FillSettlement(
            EscrowConsumed: escrowConsumed,
            BuyerRefund: refund,
            SellerProceeds: escrowConsumed - tax,
            SalesTax: tax);
    }

    /// <summary>
    /// Credits a buy order must lock at placement: the full quantity at the limit price.
    /// </summary>
    /// <remarks>
    /// Locking at placement rather than validating at fill is what makes an order unable to
    /// fail to honour itself. The cost is that capital is tied up while the order rests, which
    /// is the intended tradeoff — a book full of orders nobody can actually pay for is worse
    /// than a book that reflects committed money.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If quantity or price is not positive.</exception>
    public static Credits EscrowRequired(Credits limitUnitPrice, int quantity)
    {
        if (!limitUnitPrice.IsPositive)
        {
            throw new ArgumentOutOfRangeException(
                nameof(limitUnitPrice), limitUnitPrice, "Limit price must be positive.");
        }

        return MarketFees.GrossValue(limitUnitPrice, quantity);
    }
}
