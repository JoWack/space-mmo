using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Market;

namespace SpaceMMO.Data.Market;

/// <summary>A request to place a limit order.</summary>
/// <param name="CharacterId">Who is placing it.</param>
/// <param name="StationId">Which station's book.</param>
/// <param name="ItemDefId">Which item.</param>
/// <param name="Side">Buy or sell.</param>
/// <param name="LimitPrice">Worst acceptable unit price.</param>
/// <param name="Quantity">Units sought.</param>
/// <param name="GoodForDays">How long the remainder rests before expiring.</param>
public readonly record struct PlaceOrderRequest(
    int CharacterId,
    int StationId,
    int ItemDefId,
    OrderSide Side,
    Credits LimitPrice,
    int Quantity,
    int GoodForDays = 30);

/// <summary>The outcome of placing an order.</summary>
/// <param name="OrderId">
/// The persisted order. Always present: every order gets a row even when it fills instantly,
/// so that trades always reference two real orders and players have complete order history.
/// </param>
/// <param name="QuantityFilled">Units executed immediately.</param>
/// <param name="QuantityResting">Units left on the book. Zero means the order filled completely.</param>
/// <param name="TradeIds">Trades written, in execution order.</param>
public readonly record struct PlaceOrderResult(
    long OrderId,
    int QuantityFilled,
    int QuantityResting,
    IReadOnlyList<long> TradeIds)
{
    /// <summary>True if nothing remains on the book.</summary>
    public bool IsFullyFilled => QuantityResting == 0;
}

/// <summary>
/// Places orders atomically, per economy-design §5.
/// </summary>
/// <remarks>
/// <para>
/// This class exists to make <see cref="MatchingEngine"/>'s decisions durable without letting
/// two concurrent placements both consume the same resting quantity. It does not re-implement
/// any matching logic — that would be two sources of truth for the most correctness-critical
/// rule in the game.
/// </para>
/// <para>
/// <strong>The locking protocol, which is the whole point of the class:</strong> the candidate
/// resting orders are read with <c>FOR UPDATE</c> inside a transaction. A second transaction
/// running the same query blocks until the first commits, and then re-reads the decremented
/// quantities rather than the stale ones. Without that, two buyers could each see five
/// available units and each be told they bought five, creating items from nothing.
/// </para>
/// <para>
/// <strong>Not yet implemented: settlement.</strong> Orders, fills, and trades are recorded,
/// but credits and items do not move yet — that needs credit escrow on buy orders and
/// inventory reservation on sell orders, which is the next increment. Nothing here writes
/// ledger entries, so the money supply is unaffected in the meantime.
/// </para>
/// </remarks>
public sealed class MarketService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    /// <summary>
    /// Matches an order against the book and rests any remainder, in one transaction.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If quantity or price is not positive.</exception>
    public async Task<PlaceOrderResult> PlaceOrderAsync(
        PlaceOrderRequest request, CancellationToken cancellationToken = default)
    {
        if (request.Quantity <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(request), request.Quantity, "Order quantity must be positive.");
        }

        if (!request.LimitPrice.IsPositive)
        {
            throw new ArgumentOutOfRangeException(
                nameof(request), request.LimitPrice, "Limit price must be positive.");
        }

        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        int starSystemId = await _database.Stations
            .Where(s => s.Id == request.StationId)
            .Select(s => s.StarSystemId)
            .SingleAsync(cancellationToken);

        List<MarketOrder> candidates = await LoadAndLockCandidatesAsync(request, cancellationToken);

        // The pure engine decides everything; this method only persists the decision.
        MatchResult match = MatchingEngine.Match(
            new MatchRequest(request.CharacterId, request.Side, request.LimitPrice, request.Quantity),
            [.. candidates.Select(ToRestingOrder)]);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        // The incoming order is persisted before its trades, even when it fills entirely.
        // Recording only the unfilled remainder would leave a taker's trades pointing at no
        // order at all, which breaks the audit trail precisely for the trades that matter most.
        // The partial index on the book is filtered to quantity_remaining > 0, so a fully
        // filled order drops out of the order book automatically at no cost.
        var incomingOrder = new MarketOrder
        {
            StationId = request.StationId,
            StarSystemId = starSystemId,
            ItemDefId = request.ItemDefId,
            CharacterId = request.CharacterId,
            Side = request.Side,
            Price = request.LimitPrice,
            QuantityOriginal = request.Quantity,
            QuantityRemaining = match.QuantityUnfilled,
            PlacedAt = now,
            ExpiresAt = now.AddDays(request.GoodForDays),
        };

        _database.MarketOrders.Add(incomingOrder);
        await _database.SaveChangesAsync(cancellationToken);

        var trades = new List<Trade>(match.Fills.Count);

        foreach (Fill fill in match.Fills)
        {
            MarketOrder resting = candidates.Single(o => o.Id == fill.RestingOrderId);

            // Guarded rather than assumed: if this ever trips, the lock did not hold and the
            // failure must be loud instead of an overfilled order.
            if (fill.Quantity > resting.QuantityRemaining)
            {
                throw new InvalidOperationException(
                    $"Fill of {fill.Quantity} exceeds {resting.QuantityRemaining} remaining on "
                    + $"order {resting.Id}. Row locking failed.");
            }

            resting.QuantityRemaining -= fill.Quantity;

            bool incomingIsBuy = request.Side == OrderSide.Buy;

            var trade = new Trade
            {
                BuyOrderId = incomingIsBuy ? incomingOrder.Id : resting.Id,
                SellOrderId = incomingIsBuy ? resting.Id : incomingOrder.Id,
                StationId = request.StationId,
                StarSystemId = starSystemId,
                ItemDefId = request.ItemDefId,
                BuyerCharacterId = incomingIsBuy ? request.CharacterId : resting.CharacterId,
                SellerCharacterId = incomingIsBuy ? resting.CharacterId : request.CharacterId,
                Quantity = fill.Quantity,
                Price = fill.Price,
                SalesTax = MarketFees.SalesTax(fill.Price, fill.Quantity),
                ExecutedAt = now,
            };

            trades.Add(trade);
            _database.Trades.Add(trade);
        }

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new PlaceOrderResult(
            OrderId: incomingOrder.Id,
            QuantityFilled: match.QuantityFilled,
            QuantityResting: match.QuantityUnfilled,
            TradeIds: [.. trades.Select(t => t.Id)]);
    }

    /// <summary>
    /// Reads the crossing orders on the opposite side and locks them for the transaction.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <c>FOR UPDATE</c> is the load-bearing clause, and it requires raw SQL — EF Core has no
    /// LINQ equivalent.
    /// </para>
    /// <para>
    /// Deliberately unbounded: a <c>LIMIT</c> would be faster but could lock fewer rows than a
    /// large order needs, reopening exactly the race this exists to close. Ordering is left to
    /// the pure engine so that price-time priority lives in one place only.
    /// </para>
    /// </remarks>
    private async Task<List<MarketOrder>> LoadAndLockCandidatesAsync(
        PlaceOrderRequest request, CancellationToken cancellationToken)
    {
        long limitMinorUnits = request.LimitPrice.MinorUnits;

        // The two branches differ only in the price comparison, but they have to be written
        // out: FromSqlInterpolated parameterises every hole, so an operator cannot be
        // interpolated. That is a feature — it is also what makes this injection-proof.
        // Assigned in branches rather than by ternary: an interpolated string only becomes a
        // FormattableString when the target type is known, and a conditional expression
        // resolves to string first.
        FormattableString sql;

        if (request.Side == OrderSide.Buy)
        {
            sql = $"""
                SELECT * FROM market_orders
                WHERE station_id = {request.StationId}
                  AND item_def_id = {request.ItemDefId}
                  AND side = 'Sell'
                  AND quantity_remaining > 0
                  AND cancelled_at IS NULL
                  AND expires_at > NOW()
                  AND character_id <> {request.CharacterId}
                  AND price <= {limitMinorUnits}
                FOR UPDATE
                """;
        }
        else
        {
            sql = $"""
                SELECT * FROM market_orders
                WHERE station_id = {request.StationId}
                  AND item_def_id = {request.ItemDefId}
                  AND side = 'Buy'
                  AND quantity_remaining > 0
                  AND cancelled_at IS NULL
                  AND expires_at > NOW()
                  AND character_id <> {request.CharacterId}
                  AND price >= {limitMinorUnits}
                FOR UPDATE
                """;
        }

        return await _database.MarketOrders.FromSqlInterpolated(sql).ToListAsync(cancellationToken);
    }

    private static RestingOrder ToRestingOrder(MarketOrder order) => new(
        OrderId: order.Id,
        CharacterId: order.CharacterId,
        Side: order.Side,
        Price: order.Price,
        QuantityRemaining: order.QuantityRemaining,
        PlacedAt: order.PlacedAt);
}
