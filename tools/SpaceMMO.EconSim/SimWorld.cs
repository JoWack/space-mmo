using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Market;

namespace SpaceMMO.EconSim;

/// <summary>The items the simulation trades. Keys match <c>data/items/core.json</c>.</summary>
public static class Sim
{
    public const string Ore = "ferrite_ore";
    public const string Plate = "ferrite_plate";
    public const string Section = "shuttle_hull_section";

    public static readonly string[] TradedItems = [Ore, Plate, Section];
}

/// <summary>One simulated player.</summary>
public sealed class SimCharacter(int id, string archetype)
{
    public int Id { get; } = id;

    public string Archetype { get; } = archetype;

    public Credits Balance { get; set; }

    /// <summary>Item key to quantity held.</summary>
    public Dictionary<string, int> Inventory { get; } = new(StringComparer.Ordinal);

    /// <summary>Skill key to accumulated XP.</summary>
    public Dictionary<string, long> Xp { get; } = new(StringComparer.Ordinal);

    public int Held(string item) => Inventory.GetValueOrDefault(item);

    public void Add(string item, int quantity) =>
        Inventory[item] = Held(item) + quantity;

    public void Remove(string item, int quantity)
    {
        int remaining = Held(item) - quantity;

        if (remaining < 0)
        {
            throw new InvalidOperationException(
                $"Character {Id} cannot give up {quantity} {item}; holds {Held(item)}.");
        }

        Inventory[item] = remaining;
    }
}

/// <summary>A single trade, kept for the price index.</summary>
public readonly record struct SimTrade(int Day, string Item, int Quantity, Credits Price);

/// <summary>
/// The simulated economy.
/// </summary>
/// <remarks>
/// <para>
/// Composes the pure rules from <c>SpaceMMO.Domain</c> rather than reimplementing them, so what
/// the simulation measures is what the game actually does. Where this file contains arithmetic,
/// it is bookkeeping — never a second copy of a game rule.
/// </para>
/// <para>
/// Simulated at <strong>day granularity</strong>. Gathering is rate-limited by wall clock and
/// industry jobs run offline, so a day's output is computable in closed form from the same
/// functions the server uses. Stepping second by second would be more faithful and roughly a
/// hundred thousand times slower for no additional insight.
/// </para>
/// </remarks>
public sealed class SimWorld
{
    private readonly SimulationConfig _config;
    private readonly List<RestingOrder> _book = [];
    private long _nextOrderId = 1;

    public SimWorld(SimulationConfig config)
    {
        _config = config ?? throw new ArgumentNullException(nameof(config));

        int id = 1;

        for (int i = 0; i < config.Miners; i++)
        {
            Characters.Add(NewCharacter(id++, "miner"));
        }

        for (int i = 0; i < config.Refiners; i++)
        {
            Characters.Add(NewCharacter(id++, "refiner"));
        }

        for (int i = 0; i < config.Crafters; i++)
        {
            Characters.Add(NewCharacter(id++, "crafter"));
        }

        for (int i = 0; i < config.Traders; i++)
        {
            Characters.Add(NewCharacter(id++, "trader"));
        }

        for (int i = 0; i < config.Pilots; i++)
        {
            Characters.Add(NewCharacter(id++, "pilot"));
        }
    }

    public List<SimCharacter> Characters { get; } = [];

    /// <summary>Every credit movement, mirroring the real append-only ledger.</summary>
    public List<(int CharacterId, Credits Delta, LedgerReason Reason)> Ledger { get; } = [];

    public List<SimTrade> Trades { get; } = [];

    /// <summary>Units gathered, per item. The only place material enters.</summary>
    public Dictionary<string, long> Gathered { get; } = new(StringComparer.Ordinal);

    /// <summary>Units destroyed, per item. Crafting inputs and simulated losses.</summary>
    public Dictionary<string, long> Destroyed { get; } = new(StringComparer.Ordinal);

    /// <summary>Units produced by crafting, per item.</summary>
    public Dictionary<string, long> Crafted { get; } = new(StringComparer.Ordinal);

    /// <summary>Credits currently locked in resting buy orders.</summary>
    public Credits Escrow { get; private set; } = Credits.Zero;

    /// <summary>Last traded price per item, seeded from the design bible's targets.</summary>
    public Dictionary<string, Credits> LastPrice { get; } = new(StringComparer.Ordinal)
    {
        [Sim.Ore] = Credits.FromWholeCredits(40),
        [Sim.Plate] = Credits.FromWholeCredits(250),
        [Sim.Section] = Credits.FromWholeCredits(1_400),
    };

    /// <summary>
    /// Ore the shared deposits can yield in one day.
    /// </summary>
    /// <remarks>
    /// The hard ceiling on material entering the economy, set entirely by node capacity, respawn
    /// time, and node count. No amount of player effort exceeds it — which makes it the single
    /// most important number in the whole simulation.
    /// </remarks>
    public int DailyNodeCapacity =>
        (int)((long)_config.NodeCapacity * _config.NodeCount * 86_400 / _config.NodeRespawnSeconds);

    private SimCharacter NewCharacter(int id, string archetype)
    {
        var character = new SimCharacter(id, archetype) { Balance = _config.BootstrapCredits };

        // The onboarding chain, paid once. Uncapped by design — see economy-design §2a.
        Ledger.Add((id, _config.BootstrapCredits, LedgerReason.StoryReward));

        return character;
    }

    // ── Material ─────────────────────────────────────────────────────────────

    /// <summary>Records material entering the economy.</summary>
    public void RecordGathered(SimCharacter character, string item, int quantity)
    {
        character.Add(item, quantity);
        Gathered[item] = Gathered.GetValueOrDefault(item) + quantity;
    }

    /// <summary>Records material leaving it.</summary>
    public void RecordDestroyed(SimCharacter character, string item, int quantity)
    {
        character.Remove(item, quantity);
        Destroyed[item] = Destroyed.GetValueOrDefault(item) + quantity;
    }

    /// <summary>Records manufactured output.</summary>
    public void RecordCrafted(SimCharacter character, string item, int quantity)
    {
        character.Add(item, quantity);
        Crafted[item] = Crafted.GetValueOrDefault(item) + quantity;
    }

    // ── Credits ──────────────────────────────────────────────────────────────

    /// <summary>Moves credits and writes the matching ledger entry.</summary>
    public void Adjust(SimCharacter character, Credits delta, LedgerReason reason)
    {
        if (delta.IsZero)
        {
            return;
        }

        character.Balance += delta;
        Ledger.Add((character.Id, delta, reason));
    }

    // ── Market ───────────────────────────────────────────────────────────────

    /// <summary>
    /// Places a limit order, matching it against the book and resting the remainder.
    /// </summary>
    /// <remarks>
    /// Uses <see cref="MatchingEngine"/> and <see cref="Settlement"/> directly. If the simulation
    /// and the server ever disagree about who gets paid what, it will be because someone changed
    /// one of those — not because this file drifted.
    /// </remarks>
    public void PlaceOrder(
        SimCharacter character, OrderSide side, string item, Credits limitPrice, int quantity, int day)
    {
        if (quantity <= 0 || !limitPrice.IsPositive)
        {
            return;
        }

        Credits brokerFee = MarketFees.BrokerFee(limitPrice, quantity);

        if (character.Balance < brokerFee)
        {
            return;
        }

        if (side == OrderSide.Buy)
        {
            Credits escrowNeeded = Settlement.EscrowRequired(limitPrice, quantity);

            if (character.Balance < escrowNeeded + brokerFee)
            {
                return;
            }

            Adjust(character, -escrowNeeded, LedgerReason.MarketEscrowLocked);
            Escrow += escrowNeeded;
        }
        else
        {
            if (character.Held(item) < quantity)
            {
                return;
            }

            character.Remove(item, quantity);
        }

        Adjust(character, -brokerFee, LedgerReason.BrokerFee);

        List<RestingOrder> candidates = [.. _book.Where(o => BookItem(o.OrderId) == item)];

        MatchResult match = MatchingEngine.Match(
            new MatchRequest(character.Id, side, limitPrice, quantity), candidates);

        int remaining = quantity;

        foreach (Fill fill in match.Fills)
        {
            SimCharacter counterparty = Characters.First(c => c.Id == fill.RestingCharacterId);

            Credits escrowPrice = side == OrderSide.Buy ? limitPrice : fill.Price;

            FillSettlement settlement = Settlement.ForFill(escrowPrice, fill.Price, fill.Quantity);

            SimCharacter buyer = side == OrderSide.Buy ? character : counterparty;
            SimCharacter seller = side == OrderSide.Buy ? counterparty : character;

            // Escrow pays the seller and the tax; the surplus goes back to the buyer.
            Escrow -= settlement.TotalEscrowReleased;

            // Booked as gross proceeds and then the tax, rather than net. The player's balance
            // ends up identical either way, but this way the destroyed credits carry a sink
            // reason and faucet-versus-sink attribution actually adds up.
            Adjust(seller, settlement.EscrowConsumed, LedgerReason.MarketSale);
            Adjust(seller, -settlement.SalesTax, LedgerReason.SalesTax);

            if (settlement.BuyerRefund.IsPositive)
            {
                Adjust(buyer, settlement.BuyerRefund, LedgerReason.MarketEscrowReleased);
            }

            buyer.Add(item, fill.Quantity);

            ConsumeRestingOrder(fill.RestingOrderId, fill.Quantity);

            Trades.Add(new SimTrade(day, item, fill.Quantity, fill.Price));
            LastPrice[item] = fill.Price;

            remaining -= fill.Quantity;
        }

        if (remaining <= 0)
        {
            return;
        }

        long orderId = _nextOrderId++;
        _bookItems[orderId] = item;

        _book.Add(new RestingOrder(
            orderId, character.Id, side, limitPrice, remaining, DateTimeOffset.UnixEpoch.AddDays(day)));

        if (side == OrderSide.Sell)
        {
            _restingGoods[orderId] = remaining;
        }
    }

    /// <summary>
    /// Expires resting orders older than a week, returning escrow and goods.
    /// </summary>
    /// <remarks>
    /// Without this, escrowed credits and reserved goods would accumulate off both the balance
    /// sheet and the shelves, and the conservation checks would drift for a reason that has
    /// nothing to do with the economy being wrong.
    /// </remarks>
    public void ExpireOrders(int day)
    {
        var cutoff = DateTimeOffset.UnixEpoch.AddDays(day - 7);

        foreach (RestingOrder order in _book.Where(o => o.PlacedAt < cutoff).ToList())
        {
            SimCharacter owner = Characters.First(c => c.Id == order.CharacterId);

            if (order.Side == OrderSide.Buy)
            {
                Credits held = order.Price * order.QuantityRemaining;

                Escrow -= held;
                Adjust(owner, held, LedgerReason.MarketEscrowReleased);
            }
            else
            {
                owner.Add(BookItem(order.OrderId), order.QuantityRemaining);
                _restingGoods.Remove(order.OrderId);
            }

            _book.Remove(order);
            _bookItems.Remove(order.OrderId);
        }
    }

    /// <summary>Goods sitting in resting sell orders, which still exist and must be counted.</summary>
    public int GoodsOnBook(string item) =>
        _book.Where(o => o.Side == OrderSide.Sell && BookItem(o.OrderId) == item)
            .Sum(o => o.QuantityRemaining);

    private readonly Dictionary<long, string> _bookItems = [];
    private readonly Dictionary<long, int> _restingGoods = [];

    private string BookItem(long orderId) => _bookItems.GetValueOrDefault(orderId, string.Empty);

    private void ConsumeRestingOrder(long orderId, int quantity)
    {
        int index = _book.FindIndex(o => o.OrderId == orderId);

        if (index < 0)
        {
            return;
        }

        RestingOrder order = _book[index];
        int remaining = order.QuantityRemaining - quantity;

        // Escrow is released by the settlement loop, which knows the price it was locked at.
        // Releasing it here as well double-counted every fill against a resting buy order and
        // drove the money supply negative.
        if (order.Side == OrderSide.Sell)
        {
            _restingGoods[orderId] = remaining;
        }

        if (remaining <= 0)
        {
            _book.RemoveAt(index);
            _bookItems.Remove(orderId);
            _restingGoods.Remove(orderId);

            return;
        }

        _book[index] = order with { QuantityRemaining = remaining };
    }

    /// <summary>
    /// True if a character already has an order resting for this item and side.
    /// </summary>
    /// <remarks>
    /// Bots check this before placing, because the broker fee is charged on placement whether or
    /// not an order ever fills. Re-listing the same stock daily is a way to pay the fee repeatedly
    /// for nothing, and no real player would do it.
    /// </remarks>
    public bool HasRestingOrder(int characterId, string item, OrderSide side) =>
        _book.Any(o => o.CharacterId == characterId
            && o.Side == side
            && BookItem(o.OrderId) == item);

    /// <summary>Best resting ask for an item, or null if nobody is selling.</summary>
    public Credits? BestAsk(string item) =>
        _book.Where(o => o.Side == OrderSide.Sell && BookItem(o.OrderId) == item)
            .Select(o => (Credits?)o.Price)
            .DefaultIfEmpty(null)
            .Min();

    /// <summary>Best resting bid for an item, or null if nobody is buying.</summary>
    public Credits? BestBid(string item) =>
        _book.Where(o => o.Side == OrderSide.Buy && BookItem(o.OrderId) == item)
            .Select(o => (Credits?)o.Price)
            .DefaultIfEmpty(null)
            .Max();
}
