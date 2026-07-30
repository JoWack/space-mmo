using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Domain.Market;

/// <summary>
/// A live order sitting on the book, as loaded for matching.
/// </summary>
/// <param name="OrderId">Database identifier, used as the final ordering tiebreak.</param>
/// <param name="CharacterId">Owner, for self-trade prevention.</param>
/// <param name="Side">Which side of the book this order rests on.</param>
/// <param name="Price">Unit price. Fills execute at this price, not the taker's.</param>
/// <param name="QuantityRemaining">Unfilled quantity.</param>
/// <param name="PlacedAt">Used for time priority at equal price.</param>
public readonly record struct RestingOrder(
    long OrderId,
    int CharacterId,
    OrderSide Side,
    Credits Price,
    int QuantityRemaining,
    DateTimeOffset PlacedAt);

/// <summary>
/// An incoming limit order seeking a match.
/// </summary>
/// <param name="CharacterId">Who is placing it.</param>
/// <param name="Side">Buy or sell.</param>
/// <param name="LimitPrice">Worst acceptable price: a maximum when buying, a minimum when selling.</param>
/// <param name="Quantity">How many units are sought.</param>
public readonly record struct MatchRequest(
    int CharacterId,
    OrderSide Side,
    Credits LimitPrice,
    int Quantity);

/// <summary>One execution against a single resting order.</summary>
/// <param name="RestingOrderId">The resting order that was hit.</param>
/// <param name="RestingCharacterId">Its owner — the counterparty.</param>
/// <param name="Quantity">Units traded.</param>
/// <param name="Price">Execution price, which is always the resting order's price.</param>
public readonly record struct Fill(
    long RestingOrderId,
    int RestingCharacterId,
    int Quantity,
    Credits Price);

/// <summary>The outcome of matching one incoming order.</summary>
/// <param name="Fills">Executions, in the order they occurred.</param>
/// <param name="QuantityUnfilled">Units left over, which rest on the book as a new order.</param>
public readonly record struct MatchResult(IReadOnlyList<Fill> Fills, int QuantityUnfilled)
{
    /// <summary>Total units executed.</summary>
    public int QuantityFilled
    {
        get
        {
            int total = 0;
            foreach (Fill fill in Fills)
            {
                total += fill.Quantity;
            }

            return total;
        }
    }

    /// <summary>True if nothing is left to rest on the book.</summary>
    public bool IsFullyFilled => QuantityUnfilled == 0;

    /// <summary>True if no executions occurred at all.</summary>
    public bool IsUnmatched => Fills.Count == 0;

    /// <summary>
    /// Total credits changing hands, before fees.
    /// </summary>
    public Credits GrossValue
    {
        get
        {
            Credits total = Credits.Zero;
            foreach (Fill fill in Fills)
            {
                total += fill.Price * fill.Quantity;
            }

            return total;
        }
    }
}

/// <summary>
/// Order matching, per economy-design §5.
/// </summary>
/// <remarks>
/// <para>
/// Pure: matching decides <em>what</em> should happen, and the data layer is responsible for
/// making it happen atomically under row locks. Keeping the algorithm free of I/O is what
/// makes it cheap to test exhaustively, and matching correctness is worth exhausting —
/// the failure modes here are duplicated items and duplicated money.
/// </para>
/// <para><strong>Preconditions the caller must guarantee:</strong></para>
/// <list type="bullet">
/// <item>Every order in <c>restingBook</c> is for the same station and item as the request.
/// There is no station or item on <see cref="RestingOrder"/> deliberately — the data layer
/// queries scoped to one book, which makes crossing books structurally impossible rather
/// than merely checked.</item>
/// <item>The incoming order is not itself in the book.</item>
/// </list>
/// </remarks>
public static class MatchingEngine
{
    /// <summary>
    /// Matches an incoming order against a resting book.
    /// </summary>
    /// <remarks>
    /// Three rules, each a deliberate choice:
    /// <list type="number">
    /// <item><strong>Price-time priority.</strong> Best price first; at equal price, oldest
    /// first. Ties on timestamp break by order id, so the result is fully deterministic even
    /// when two orders land in the same clock tick.</item>
    /// <item><strong>Fills execute at the resting order's price.</strong> The standard
    /// maker-price convention: price improvement goes to the incoming order, and a player who
    /// posted a patient order gets exactly the price they asked for.</item>
    /// <item><strong>Self-trade prevention.</strong> A character never matches their own
    /// resting orders — see <see cref="SkipsOwnOrders"/>.</item>
    /// </list>
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If quantity is not positive or the limit price is not positive.
    /// </exception>
    public static MatchResult Match(MatchRequest request, IReadOnlyList<RestingOrder> restingBook)
    {
        ArgumentNullException.ThrowIfNull(restingBook);

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

        List<RestingOrder> candidates = SelectCandidates(request, restingBook);

        var fills = new List<Fill>();
        int remaining = request.Quantity;

        foreach (RestingOrder candidate in candidates)
        {
            if (remaining == 0)
            {
                break;
            }

            int quantity = Math.Min(remaining, candidate.QuantityRemaining);

            fills.Add(new Fill(
                RestingOrderId: candidate.OrderId,
                RestingCharacterId: candidate.CharacterId,
                Quantity: quantity,
                Price: candidate.Price));

            remaining -= quantity;
        }

        return new MatchResult(fills, remaining);
    }

    /// <summary>
    /// True — a character's incoming order never matches their own resting orders.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Wash trading is not free money here, because both sides pay fees, so it is
    /// self-limiting as an exploit. It is blocked for a different reason: trade history is the
    /// price signal every other player reads before deciding what to build or haul, and a
    /// player who can print fake trades at arbitrary prices can manipulate that signal
    /// cheaply.
    /// </para>
    /// <para>
    /// Exposed as a named constant so the reasoning has somewhere to live and tests can assert
    /// the behaviour is intentional rather than incidental.
    /// </para>
    /// </remarks>
    public const bool SkipsOwnOrders = true;

    /// <summary>
    /// Filters the book to orders that can actually trade, then sorts them into match order.
    /// </summary>
    private static List<RestingOrder> SelectCandidates(
        MatchRequest request, IReadOnlyList<RestingOrder> restingBook)
    {
        OrderSide opposite = request.Side == OrderSide.Buy ? OrderSide.Sell : OrderSide.Buy;

        var candidates = new List<RestingOrder>();

        foreach (RestingOrder order in restingBook)
        {
            if (order.Side != opposite)
            {
                continue;
            }

            if (order.QuantityRemaining <= 0)
            {
                continue;
            }

            if (SkipsOwnOrders && order.CharacterId == request.CharacterId)
            {
                continue;
            }

            bool crosses = request.Side == OrderSide.Buy
                ? order.Price <= request.LimitPrice
                : order.Price >= request.LimitPrice;

            if (crosses)
            {
                candidates.Add(order);
            }
        }

        // A buyer wants the cheapest ask first; a seller wants the highest bid first. Time
        // then order id break ties, so matching is deterministic for a given book.
        candidates.Sort((left, right) =>
        {
            int byPrice = request.Side == OrderSide.Buy
                ? left.Price.CompareTo(right.Price)
                : right.Price.CompareTo(left.Price);

            if (byPrice != 0)
            {
                return byPrice;
            }

            int byTime = left.PlacedAt.CompareTo(right.PlacedAt);

            return byTime != 0 ? byTime : left.OrderId.CompareTo(right.OrderId);
        });

        return candidates;
    }
}
