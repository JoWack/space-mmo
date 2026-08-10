using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Market;

namespace SpaceMMO.EconSim;

/// <summary>The items the simulation trades. Keys match <c>data/items/core.json</c>.</summary>
public static class Sim
{
    public const string Ore = "ferrite_ore";
    public const string Plate = "ferrite_plate";
    public const string Section = "shuttle_hull_section";

    /// <summary>The finished good that cannot be built inside one faction.</summary>
    /// <remarks>
    /// <c>build_alloy_frame</c> consumes ten of each planet-locked ore, and each ore is gatherable
    /// on exactly one homeworld — two of them in each faction. So a frame is the point at which
    /// material has to cross the faction line, and the only reason anyone from A ever needs to buy
    /// from B. ADR-0008 rests on this: if frames stop being worth building, the crossing stops
    /// happening and the PvP zone becomes decoration.
    /// </remarks>
    public const string Frame = "alloy_frame";

    public const string TerranFerrite = "terran_ferrite";
    public const string AresRegolith = "ares_regolith";
    public const string VerdantAmber = "verdant_amber";
    public const string GrimholdSlag = "grimhold_slag";

    /// <summary>The four ores, one per homeworld, in the order the recipe lists them.</summary>
    public static readonly string[] PlanetLockedOres =
        [TerranFerrite, AresRegolith, VerdantAmber, GrimholdSlag];

    /// <summary>Ten of each, from <c>build_alloy_frame</c> in <c>data/recipes/core.json</c>.</summary>
    public const int OrePerFrame = 10;

    public static readonly string[] TradedItems =
        [Ore, Plate, Section, Frame, .. PlanetLockedOres];

    /// <summary>Market venues: the four homeworlds and the shared capital.</summary>
    public const string Capital = "body_capital";

    /// <summary>The ore a homeworld yields. Nowhere else has it.</summary>
    public static string OreOf(string bodyKey) => bodyKey switch
    {
        "body_terra" => TerranFerrite,
        "body_ares" => AresRegolith,
        "body_verdance" => VerdantAmber,
        "body_grimhold" => GrimholdSlag,
        _ => throw new ArgumentOutOfRangeException(
            nameof(bodyKey), bodyKey, "No planet-locked ore for this body."),
    };
}

/// <summary>One simulated player.</summary>
public sealed class SimCharacter(int id, string archetype, Race race)
{
    public int Id { get; } = id;

    public string Archetype { get; } = archetype;

    /// <summary>
    /// Race, which fixes faction and homeworld.
    /// </summary>
    /// <remarks>
    /// Faction and home body are derived through <see cref="Races"/> rather than stored, for the
    /// same reason the server derives them: a stored triple can claim a Space Orc in Faction A.
    /// </remarks>
    public Race Race { get; } = race;

    public Faction Faction => Races.FactionFor(Race);

    public string HomeBody => Races.HomeBodyKeyFor(Race);

    /// <summary>The planet-locked ore this character can gather, and nobody else can.</summary>
    public string HomeOre => Sim.OreOf(HomeBody);

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
/// <remarks>
/// Carries the two factions rather than just the price, because the question ADR-0008 needs
/// answered is not "what did ore cost" but "did any of it change hands across the line".
/// </remarks>
public readonly record struct SimTrade(
    int Day,
    string Market,
    string Item,
    int Quantity,
    Credits Price,
    Faction BuyerFaction,
    Faction SellerFaction)
{
    /// <summary>True if this trade moved goods between the two factions.</summary>
    public bool IsCrossFaction => BuyerFaction != SellerFaction;
}

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

        // Race is assigned per archetype rather than across the whole population, so each
        // profession is spread evenly over the four homeworlds. Assigning it globally would leave
        // whole planets with, say, no refiners, and the resulting dead local book would look like a
        // balance finding rather than the seeding artefact it is.
        AddArchetype("miner", config.Miners);
        AddArchetype("refiner", config.Refiners);
        AddArchetype("crafter", config.Crafters);
        AddArchetype("framewright", config.Framewrights);
        AddArchetype("trader", config.Traders);
        AddArchetype("pilot", config.Pilots);

        void AddArchetype(string archetype, int count)
        {
            for (int i = 0; i < count; i++)
            {
                Characters.Add(NewCharacter(id++, archetype, (Race)(i % 4)));
            }
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

    private readonly Dictionary<(string Market, string Item), Credits> _lastPrice = [];

    /// <summary>
    /// Last traded price for an item <em>at one market</em>, seeded from the design bible's targets.
    /// </summary>
    /// <remarks>
    /// Per market, not global. A single price index would defeat the whole point of splitting the
    /// books: the capital's premium and a homeworld's local discount only exist as the difference
    /// between two prices, and averaging them away would report a healthy market that nobody is
    /// actually trading in.
    /// </remarks>
    public Credits LastPrice(string market, string item) =>
        _lastPrice.TryGetValue((market, item), out Credits price) ? price : SeedPrice(item);

    /// <summary>Opening price for an item nobody has traded yet.</summary>
    private static Credits SeedPrice(string item) => item switch
    {
        Sim.Ore => Credits.FromWholeCredits(40),
        Sim.Plate => Credits.FromWholeCredits(250),
        Sim.Section => Credits.FromWholeCredits(1_400),

        // Each comes from one planet rather than everywhere, so it opens above common ore.
        Sim.TerranFerrite or Sim.AresRegolith or Sim.VerdantAmber or Sim.GrimholdSlag =>
            Credits.FromWholeCredits(60),

        // Forty ore at the seed price, plus a margin for the work of collecting them from four
        // planets across a faction line.
        Sim.Frame => Credits.FromWholeCredits(3_000),

        _ => Credits.FromWholeCredits(100),
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

    private SimCharacter NewCharacter(int id, string archetype, Race race)
    {
        var character = new SimCharacter(id, archetype, race) { Balance = _config.BootstrapCredits };

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
        SimCharacter character,
        string market,
        OrderSide side,
        string item,
        Credits limitPrice,
        int quantity,
        int day)
    {
        if (quantity <= 0 || !limitPrice.IsPositive)
        {
            return;
        }

        Credits brokerFee = MarketFees.BrokerFee(limitPrice, quantity);

        // The capital charges more to list. It is the only venue where all four planet-locked ores
        // meet, so without a premium it would swallow every local book by being strictly better —
        // and a game where all trade happens in one room does not need four planets.
        if (market == Sim.Capital)
        {
            brokerFee += brokerFee.PercentRoundedUp(_config.CapitalFeePremiumBasisPoints);
        }

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

        // Only orders at this market. An order resting on Terra is not reachable from Grimhold, and
        // matching across venues would silently rebuild the single global book this split exists to
        // get rid of.
        List<RestingOrder> candidates =
            [.. _book.Where(o => BookItem(o.OrderId) == item && BookMarket(o.OrderId) == market)];

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

            Trades.Add(new SimTrade(
                day, market, item, fill.Quantity, fill.Price, buyer.Faction, seller.Faction));

            _lastPrice[(market, item)] = fill.Price;

            remaining -= fill.Quantity;
        }

        if (remaining <= 0)
        {
            return;
        }

        long orderId = _nextOrderId++;
        _bookItems[orderId] = item;
        _bookMarkets[orderId] = market;

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
            _bookMarkets.Remove(order.OrderId);
        }
    }

    /// <summary>
    /// Goods sitting in resting sell orders, which still exist and must be counted.
    /// </summary>
    /// <remarks>
    /// Deliberately across every market. Conservation is a property of the whole economy, and
    /// counting one venue at a time would report a leak every time stock rested somewhere else.
    /// </remarks>
    public int GoodsOnBook(string item) =>
        _book.Where(o => o.Side == OrderSide.Sell && BookItem(o.OrderId) == item)
            .Sum(o => o.QuantityRemaining);

    private readonly Dictionary<long, string> _bookItems = [];
    private readonly Dictionary<long, string> _bookMarkets = [];
    private readonly Dictionary<long, int> _restingGoods = [];

    private string BookItem(long orderId) => _bookItems.GetValueOrDefault(orderId, string.Empty);

    private string BookMarket(long orderId) => _bookMarkets.GetValueOrDefault(orderId, string.Empty);

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
            _bookMarkets.Remove(orderId);
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
    public bool HasRestingOrder(int characterId, string market, string item, OrderSide side) =>
        _book.Any(o => o.CharacterId == characterId
            && o.Side == side
            && BookItem(o.OrderId) == item
            && BookMarket(o.OrderId) == market);

    /// <summary>Best resting ask for an item at one market, or null if nobody is selling.</summary>
    public Credits? BestAsk(string market, string item) =>
        _book.Where(o => o.Side == OrderSide.Sell
                && BookItem(o.OrderId) == item
                && BookMarket(o.OrderId) == market)
            .Select(o => (Credits?)o.Price)
            .DefaultIfEmpty(null)
            .Min();

    /// <summary>Best resting bid for an item at one market, or null if nobody is buying.</summary>
    public Credits? BestBid(string market, string item) =>
        _book.Where(o => o.Side == OrderSide.Buy
                && BookItem(o.OrderId) == item
                && BookMarket(o.OrderId) == market)
            .Select(o => (Credits?)o.Price)
            .DefaultIfEmpty(null)
            .Max();

    /// <summary>Resting orders at one market, for the local-book liveness report.</summary>
    public int OrdersAt(string market) =>
        _book.Count(o => BookMarket(o.OrderId) == market);
}
