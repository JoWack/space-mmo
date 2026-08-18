using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Market;

namespace SpaceMMO.Data.Market;

/// <summary>Thrown when a character cannot cover the cost of an order.</summary>
public sealed class InsufficientFundsException(int characterId, Credits required, Credits available)
    : InvalidOperationException(
        $"Character {characterId} has {available} but {required} is required.")
{
    public int CharacterId { get; } = characterId;

    public Credits Required { get; } = required;

    public Credits Available { get; } = available;
}

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
/// <param name="BrokerFee">Non-refundable fee charged at placement.</param>
/// <param name="EscrowRemaining">Credits still locked against the resting remainder.</param>
/// <param name="TradeIds">Trades written, in execution order.</param>
public readonly record struct PlaceOrderResult(
    long OrderId,
    int QuantityFilled,
    int QuantityResting,
    Credits BrokerFee,
    Credits EscrowRemaining,
    IReadOnlyList<long> TradeIds)
{
    /// <summary>True if nothing remains on the book.</summary>
    public bool IsFullyFilled => QuantityResting == 0;
}

/// <summary>
/// Places, settles, and cancels market orders, per economy-design §5.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Buy orders lock credits at placement and sell orders reserve goods.</strong> An order
/// on the book therefore always represents money or material that actually exists and has been
/// committed — it can never fail to honour itself. The cost is that capital and cargo are tied up
/// while an order rests, which is the intended tradeoff: a book full of orders nobody can pay for
/// is worse than a book that reflects real commitments.
/// </para>
/// <para>
/// <strong>Concurrency.</strong> Crossing orders are read with <c>FOR UPDATE</c>, so a competing
/// placement blocks and then re-reads decremented quantities. Balance changes go through atomic
/// <c>UPDATE … SET balance = balance + n</c> statements rather than read-modify-write, so
/// crediting two sellers concurrently cannot lose an update.
/// </para>
/// <para>
/// <strong>Matching logic lives in <see cref="MatchingEngine"/> and settlement arithmetic in
/// <see cref="Settlement"/>.</strong> This class only makes their decisions durable. Two sources
/// of truth for either would be a poor trade.
/// </para>
/// </remarks>
public sealed class MarketService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    private readonly InventoryService _inventories = new(database);

    /// <summary>
    /// Places an order: charges the broker fee, commits money or goods, matches, and settles —
    /// all in one transaction.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If quantity or price is not positive.</exception>
    /// <exception cref="InsufficientFundsException">If the buyer cannot cover escrow plus fee.</exception>
    /// <exception cref="InsufficientItemsException">If the seller lacks the goods.</exception>
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

        // Locks are always taken in the same order — character, then hangar, then market orders —
        // because inconsistent ordering is what produces deadlocks. Creating the hangar first
        // deadlocked two concurrent placements from the same character, since one would hold the
        // new hangar row while waiting for the character row the other already held.
        Credits balance = await LockAndReadBalanceAsync(request.CharacterId, cancellationToken);

        Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
            request.CharacterId, request.StationId, cancellationToken);

        // Waived only when a seller genuinely cannot pay it, and only up to a credit. Placing a
        // sell order charges its fee before anything is sold, so without this a player holding
        // nothing but goods could not turn them into money -- the one thing they needed to do.
        Credits brokerFee = MarketFees.EffectiveBrokerFee(
            MarketFees.BrokerFee(request.LimitPrice, request.Quantity),
            balance,
            request.Side);
        Credits escrowRequired = request.Side == OrderSide.Buy
            ? Settlement.EscrowRequired(request.LimitPrice, request.Quantity)
            : Credits.Zero;

        Credits upfrontCost = brokerFee + escrowRequired;

        if (balance < upfrontCost)
        {
            throw new InsufficientFundsException(request.CharacterId, upfrontCost, balance);
        }

        Credits reservedCostBasis = Credits.Zero;

        if (request.Side == OrderSide.Sell)
        {
            await TopUpFromPocketsAsync(request, hangar, cancellationToken);

            // Throws if the seller does not hold the goods, before anything else is written. The
            // returned cost basis travels with the reservation so that cancelling puts the goods
            // back at what they originally cost rather than at zero.
            reservedCostBasis = await _inventories.RemoveAsync(
                hangar.Id, request.ItemDefId, request.Quantity, cancellationToken);
        }

        List<MarketOrder> candidates = await LoadAndLockCandidatesAsync(request, cancellationToken);

        MatchResult match = MatchingEngine.Match(
            new MatchRequest(request.CharacterId, request.Side, request.LimitPrice, request.Quantity),
            [.. candidates.Select(ToRestingOrder)]);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        // Persisted even when it fills entirely: recording only the unfilled remainder would
        // leave a taker's trades pointing at no order, breaking the audit trail for exactly the
        // trades that matter most. The book's partial index excludes filled orders at no cost.
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
            EscrowedCredits = escrowRequired,
            ReservedQuantity = request.Side == OrderSide.Sell ? request.Quantity : 0,
            ReservedCostBasis = reservedCostBasis,
            PlacedAt = now,
            ExpiresAt = now.AddDays(request.GoodForDays),
        };

        _database.MarketOrders.Add(incomingOrder);
        await _database.SaveChangesAsync(cancellationToken);

        await AdjustBalanceAsync(
            request.CharacterId, -brokerFee, LedgerReason.BrokerFee, incomingOrder.Id, now,
            cancellationToken);

        if (escrowRequired.IsPositive)
        {
            await AdjustBalanceAsync(
                request.CharacterId, -escrowRequired, LedgerReason.MarketEscrowLocked,
                incomingOrder.Id, now, cancellationToken);
        }

        var trades = new List<Trade>(match.Fills.Count);

        foreach (Fill fill in match.Fills)
        {
            MarketOrder resting = candidates.Single(o => o.Id == fill.RestingOrderId);

            // Guarded rather than assumed: if this trips, the lock did not hold, and the failure
            // must be loud instead of an overfilled order.
            if (fill.Quantity > resting.QuantityRemaining)
            {
                throw new InvalidOperationException(
                    $"Fill of {fill.Quantity} exceeds {resting.QuantityRemaining} remaining on "
                    + $"order {resting.Id}. Row locking failed.");
            }

            resting.QuantityRemaining -= fill.Quantity;

            bool incomingIsBuy = request.Side == OrderSide.Buy;
            MarketOrder buyOrder = incomingIsBuy ? incomingOrder : resting;
            MarketOrder sellOrder = incomingIsBuy ? resting : incomingOrder;

            // Escrow was locked at the buy order's own limit price, so that is the rate to settle
            // against. Fills at a better price refund the difference rather than pocketing it.
            FillSettlement settlement = Settlement.ForFill(
                buyOrder.Price, fill.Price, fill.Quantity);

            buyOrder.EscrowedCredits -= settlement.TotalEscrowReleased;

            // The reserved goods leave with their share of the seller's original cost basis, which
            // is discarded: for the buyer these units now cost what they just paid, not what the
            // seller once paid for them.
            Credits soldCostBasis = ShareOfCostBasis(
                sellOrder.ReservedCostBasis, fill.Quantity, sellOrder.ReservedQuantity);

            sellOrder.ReservedQuantity -= fill.Quantity;
            sellOrder.ReservedCostBasis -= soldCostBasis;

            var trade = new Trade
            {
                BuyOrderId = buyOrder.Id,
                SellOrderId = sellOrder.Id,
                StationId = request.StationId,
                StarSystemId = starSystemId,
                ItemDefId = request.ItemDefId,
                BuyerCharacterId = buyOrder.CharacterId,
                SellerCharacterId = sellOrder.CharacterId,
                Quantity = fill.Quantity,
                Price = fill.Price,
                SalesTax = settlement.SalesTax,
                ExecutedAt = now,
            };

            _database.Trades.Add(trade);
            await _database.SaveChangesAsync(cancellationToken);
            trades.Add(trade);

            await AdjustBalanceAsync(
                sellOrder.CharacterId, settlement.SellerProceeds, LedgerReason.MarketSale,
                trade.Id, now, cancellationToken);

            if (settlement.BuyerRefund.IsPositive)
            {
                await AdjustBalanceAsync(
                    buyOrder.CharacterId, settlement.BuyerRefund,
                    LedgerReason.MarketEscrowReleased, trade.Id, now, cancellationToken);
            }

            // Goods go to the buyer's hangar at this station. The seller's copy already left
            // their inventory when the sell order was placed.
            Inventory buyerHangar = await _inventories.GetOrCreateStationHangarAsync(
                buyOrder.CharacterId, request.StationId, cancellationToken);

            // The buyer's cost basis is what they actually paid for these units — the trade value,
            // not any market reference. That is what keeps a hull later built from them insurable
            // at an honest figure (ADR-0006).
            await _inventories.AddAsync(
                buyerHangar.Id,
                request.ItemDefId,
                fill.Quantity,
                settlement.EscrowConsumed,
                cancellationToken);
        }

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new PlaceOrderResult(
            OrderId: incomingOrder.Id,
            QuantityFilled: match.QuantityFilled,
            QuantityResting: match.QuantityUnfilled,
            BrokerFee: brokerFee,
            EscrowRemaining: incomingOrder.EscrowedCredits,
            TradeIds: [.. trades.Select(t => t.Id)]);
    }

    /// <summary>
    /// Cancels an order, releasing any remaining escrow and returning any reserved goods.
    /// </summary>
    /// <remarks>
    /// The broker fee is <em>not</em> refunded. That is what makes it discourage order spam:
    /// placing and cancelling to probe the book has to cost something, or the book becomes noise.
    /// </remarks>
    /// <returns>True if the order was cancelled; false if it was already closed.</returns>
    public async Task<bool> CancelOrderAsync(
        long orderId, int characterId, CancellationToken cancellationToken = default)
    {
        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        List<MarketOrder> locked = await _database.MarketOrders
            .FromSqlInterpolated($"SELECT * FROM market_orders WHERE id = {orderId} FOR UPDATE")
            .ToListAsync(cancellationToken);

        MarketOrder order = locked.Count == 1
            ? locked[0]
            : throw new InvalidOperationException($"Order {orderId} does not exist.");

        if (order.CharacterId != characterId)
        {
            throw new UnauthorizedAccessException(
                $"Order {orderId} belongs to character {order.CharacterId}, not {characterId}.");
        }

        if (order.CancelledAt is not null)
        {
            return false;
        }

        DateTimeOffset now = DateTimeOffset.UtcNow;

        if (order.EscrowedCredits.IsPositive)
        {
            await AdjustBalanceAsync(
                order.CharacterId, order.EscrowedCredits, LedgerReason.MarketEscrowReleased,
                order.Id, now, cancellationToken);

            order.EscrowedCredits = Credits.Zero;
        }

        if (order.ReservedQuantity > 0)
        {
            Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
                order.CharacterId, order.StationId, cancellationToken);

            // Returned at their original cost, not at zero. Resetting it would let a player launder
            // an item's cost basis away by listing and cancelling.
            await _inventories.AddAsync(
                hangar.Id,
                order.ItemDefId,
                order.ReservedQuantity,
                order.ReservedCostBasis,
                cancellationToken);

            order.ReservedQuantity = 0;
            order.ReservedCostBasis = Credits.Zero;
        }

        order.CancelledAt = now;
        order.QuantityRemaining = 0;

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return true;
    }

    /// <summary>
    /// Applies a balance change and records the matching ledger entry.
    /// </summary>
    /// <remarks>
    /// The balance moves via <c>SET balance = balance + n</c> rather than a read-modify-write, so
    /// two sellers being credited concurrently cannot lose an update. The ledger entry is written
    /// in the same transaction, which is what keeps the cached balance reconcilable against the
    /// ledger that is authoritative over it (ADR-0005).
    /// </remarks>
    private async Task AdjustBalanceAsync(
        int characterId,
        Credits delta,
        LedgerReason reason,
        long? referenceId,
        DateTimeOffset at,
        CancellationToken cancellationToken)
    {
        if (delta.IsZero)
        {
            return;
        }

        long minorUnits = delta.MinorUnits;

        await _database.Database.ExecuteSqlInterpolatedAsync(
            $"UPDATE characters SET balance = balance + {minorUnits} WHERE id = {characterId}",
            cancellationToken);

        _database.LedgerEntries.Add(new LedgerEntry
        {
            CharacterId = characterId,
            DeltaCredits = delta,
            Reason = reason,
            ReferenceId = referenceId,
            CreatedAt = at,
        });

        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>
    /// Moves what a seller is carrying into the station hangar, when they are standing in it.
    /// </summary>
    /// <remarks>
    /// <para>
    /// An order fills from the hangar, so strictly a seller must put their goods down first. That is
    /// the rule and it stays the rule — but making the player drag a stack across a screen to satisfy
    /// it is a chore, not a decision: they are standing in the station holding the ore, and "I have
    /// this to sell" is what they mean. So the transfer still happens, and still leaves an honest
    /// record of where the goods went; it is simply not a job for the player.
    /// </para>
    /// <para>
    /// This is the same rule crafting follows, for the same reason — see
    /// <c>IndustryService.StartJobAsync</c>. Two ways to answer "do I have this?" at one station
    /// would be one way too many.
    /// </para>
    /// <para>
    /// <strong>Only while docked here.</strong> Otherwise selling at a station a character is
    /// nowhere near would reach into their pockets across the system.
    /// </para>
    /// </remarks>
    private async Task TopUpFromPocketsAsync(
        PlaceOrderRequest request, Inventory hangar, CancellationToken cancellationToken)
    {
        int? dockedStationId = await _database.Characters
            .Where(c => c.Id == request.CharacterId)
            .Select(c => c.DockedStationId)
            .SingleAsync(cancellationToken);

        if (dockedStationId != request.StationId)
        {
            return;
        }

        int alreadyHere = await _inventories.QuantityOfAsync(
            hangar.Id, request.ItemDefId, cancellationToken);

        if (alreadyHere >= request.Quantity)
        {
            return;
        }

        Inventory carried = await _inventories.GetOrCreateCarriedAsync(
            request.CharacterId, cancellationToken);

        int carrying = await _inventories.QuantityOfAsync(
            carried.Id, request.ItemDefId, cancellationToken);

        // Whatever closes the gap, or everything carried if that is not enough. Falling short is
        // not an error here: the removal below reports it, and reports it against the goods
        // actually missing rather than against a transfer.
        int moving = Math.Min(request.Quantity - alreadyHere, carrying);

        if (moving <= 0)
        {
            return;
        }

        // Through TransferAsync so the cost basis travels with the goods (task 99).
        await _inventories.TransferAsync(
            carried.Id, hangar.Id, request.ItemDefId, moving, cancellationToken);

        // Flushed before the removal reads it back. AddAsync and RemoveAsync mutate tracked
        // entities and leave saving to the caller, while the removal finds its stack with a
        // database query -- so a hangar that held none of this would have a row visible only in
        // the change tracker, and the sale would fail for want of goods in the same transaction.
        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>
    /// Locks a character's row and returns their balance.
    /// </summary>
    /// <remarks>
    /// The lock is what makes the sufficiency check meaningful — without it, two orders from the
    /// same character could each pass a check against the same credits and jointly overspend.
    /// </remarks>
    private async Task<Credits> LockAndReadBalanceAsync(
        int characterId, CancellationToken cancellationToken)
    {
        List<Character> locked = await _database.Characters
            .FromSqlInterpolated($"SELECT * FROM characters WHERE id = {characterId} FOR UPDATE")
            .ToListAsync(cancellationToken);

        return locked.Count == 1
            ? locked[0].Balance
            : throw new InvalidOperationException($"Character {characterId} does not exist.");
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

    /// <summary>
    /// The share of a cost basis belonging to <paramref name="quantity"/> of
    /// <paramref name="totalQuantity"/> units.
    /// </summary>
    /// <remarks>
    /// Floored, with the remainder staying behind, so splitting never creates or loses basis.
    /// </remarks>
    private static Credits ShareOfCostBasis(Credits basis, int quantity, int totalQuantity)
    {
        if (totalQuantity <= 0 || basis.IsZero)
        {
            return Credits.Zero;
        }

        if (quantity >= totalQuantity)
        {
            return basis;
        }

        return Credits.FromMinorUnits((long)((Int128)basis.MinorUnits * quantity / totalQuantity));
    }

    private static RestingOrder ToRestingOrder(MarketOrder order) => new(
        OrderId: order.Id,
        CharacterId: order.CharacterId,
        Side: order.Side,
        Price: order.Price,
        QuantityRemaining: order.QuantityRemaining,
        PlacedAt: order.PlacedAt);
}
