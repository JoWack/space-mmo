using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Gathering;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Market;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Random;

namespace SpaceMMO.EconSim;

/// <summary>
/// What each archetype does with a day.
/// </summary>
/// <remarks>
/// Bots are deliberately simple and greedy. They are not meant to be clever players — they are
/// meant to apply steady, predictable pressure so that what the numbers show is the shape of the
/// economy rather than the shape of a trading strategy.
/// </remarks>
public sealed class Bots(SimulationConfig config, SimWorld world)
{
    /// <summary>Spread bots price at, either side of the last trade.</summary>
    private const int SpreadBasisPoints = 500;

    /// <summary>Unsold frames at which a framewright stops buying more ore.</summary>
    private const int UnsoldFrameLimit = 3;

    private readonly SimulationConfig _config = config;
    private readonly SimWorld _world = world;

    /// <summary>
    /// Ore still available today, per homeworld.
    /// </summary>
    /// <remarks>
    /// Per planet, not shared. Node capacity is the hard ceiling on material entering the economy,
    /// and four planets each with their own deposits is four independent ceilings — pooling them
    /// would let Terra's miners eat Grimhold's supply and quietly erase the scarcity that makes
    /// each ore worth shipping.
    /// </remarks>
    private readonly Dictionary<string, int> _oreLeftToday = [];

    public void RunDay(int day, ref SplitMix64 rng)
    {
        foreach (string ore in Sim.PlanetLockedOres)
        {
            _oreLeftToday[ore] = _world.DailyCapacityFor(ore);
        }

        _oreLeftToday[Sim.Ore] = _world.DailyCapacityFor(Sim.Ore);

        _world.ExpireOrders(day);

        foreach (SimCharacter character in _world.Characters)
        {
            PayDailyQuestCredits(character);

            switch (character.Archetype)
            {
                case "miner":
                    RunMiner(character, day);
                    break;

                // Refiners buy their ferrite where it is mined — the capital — and sell plates at
                // home. Crafters never leave: plates and hull sections are made and consumed on the
                // same homeworld. That is what keeps four local books worth opening.
                case "refiner":
                    RunIndustry(
                        character, day, "refining",
                        Sim.Ore, Sim.OrePerRefiningRun,
                        Sim.Plate, Sim.PlatesPerRefiningRun,
                        Sim.RefiningJobSeconds,
                        inputMarket: Sim.Capital, outputMarket: character.HomeBody);
                    break;

                case "crafter":
                    RunIndustry(
                        character, day, "shipcrafting",
                        Sim.Plate, Sim.PlatesPerSectionRun,
                        Sim.Section, Sim.SectionsPerSectionRun,
                        Sim.ShipcraftingJobSeconds,
                        inputMarket: character.HomeBody, outputMarket: character.HomeBody);
                    break;

                case "framewright":
                    RunFramewright(character, day);
                    break;

                case "trader":
                    RunTrader(character, day, ref rng);
                    break;

                case "pilot":
                    RunPilot(character, day, ref rng);
                    break;

                default:
                    throw new InvalidOperationException($"Unknown archetype '{character.Archetype}'.");
            }
        }

        ApplyLosses(ref rng);
    }

    /// <summary>
    /// Whether this character can act on the capital's book today.
    /// </summary>
    /// <remarks>
    /// Flight time, expressed as how often the distant market is reachable rather than as a
    /// position. Staggered by character id so the capital sees steady traffic instead of everyone
    /// arriving on the same day and then nobody for two — which would read as a market that
    /// periodically dies rather than one that is merely far away.
    /// </remarks>
    private bool CanReachCapital(SimCharacter character, int day) =>
        _config.CapitalTripDays <= 1
        || (day + character.Id) % _config.CapitalTripDays == 0;

    /// <summary>
    /// The steady-state faucet, routed through the same daily cap the server uses.
    /// </summary>
    private void PayDailyQuestCredits(SimCharacter character)
    {
        if (!_config.DailyQuestCredits.IsPositive)
        {
            return;
        }

        FaucetGrant grant = FaucetBudget.Evaluate(_config.DailyQuestCredits, Credits.Zero);

        _world.Adjust(character, grant.Granted, LedgerReason.QuestReward);
    }

    /// <summary>
    /// Gathers ore for the day, then lists it.
    /// </summary>
    /// <remarks>
    /// Output comes straight from <see cref="GatheringYield"/>, so the simulation is constrained by
    /// the same tick rate and level scaling the server enforces.
    /// </remarks>
    private void RunMiner(SimCharacter character, int day)
    {
        // At home, the miner works its homeworld's ore, which exists nowhere else. On a capital
        // trip it works the capital's common ferrite instead. That keeps the original refining
        // chain supplied — ferrite deposits are authored on the capital, not the homeworlds — and
        // it means the ore that crosses factions is the scarce one, which is the point.
        bool atCapital = CanReachCapital(character, day);

        string market = atCapital ? Sim.Capital : character.HomeBody;
        string ore = atCapital ? Sim.Ore : character.HomeOre;

        int level = SkillCurve.LevelForXp(character.Xp.GetValueOrDefault("mining"));

        // A day of continuous play, not a single call. GatheringYield.UnitsAvailable applies the
        // 20-tick banking cap, which exists to stop one call claiming an hour of idle time — using
        // it here would model a player who gathers once and logs off, and understated raw material
        // supply roughly fortyfold.
        long ticks = _config.ActiveSecondsPerDay / GatheringYield.TickSeconds;
        long capable = ticks * GatheringYield.UnitsPerTick(level);

        int wanted = (int)Math.Min(capable, _oreLeftToday.GetValueOrDefault(ore));

        if (wanted <= 0)
        {
            return;
        }

        _oreLeftToday[ore] -= wanted;

        _world.RecordGathered(character, ore, wanted);
        character.Xp["mining"] = character.Xp.GetValueOrDefault("mining")
            + (wanted * GatheringYield.XpPerUnit);

        // Miners are not traders: they list the day's output at a slight discount to move it.
        //
        // Listing *today's production* each day, rather than skipping the listing whenever an
        // order is already resting. The earlier rule looked like fee discipline but modelled a
        // miner who lists once and then hoards forever: a single stale order sat on the book while
        // the stock behind it grew without bound. That starved the ore market no matter how much
        // was mined, and it — not the game's balance — is what drove the price to zero in §5a.
        // Raw ore is an export, so it is only ever listed at the capital.
        //
        // The first version listed home ore on its home book, which read as the obvious thing and
        // produced an economy with zero trades in it: the only buyers of planet-locked ore are
        // framewrights, and framewrights are at the capital. Sellers and buyers each behaved
        // sensibly and never once met. What a homeworld's book carries is plates and hull
        // sections — goods made locally for local pilots — not the ore underneath them.
        if (!atCapital)
        {
            return;
        }

        Credits freshAsk = AskPrice(Sim.Capital, ore, character, wanted);

        _world.PlaceOrder(
            character,
            Sim.Capital,
            OrderSide.Sell,
            ore,
            freshAsk,
            Listable(character, Sim.Capital, freshAsk, wanted),
            day);

        // Home ore accumulated since the last trip, carried in on the same journey.
        int carried = character.Held(character.HomeOre);

        if (carried <= 0
            || _world.HasRestingOrder(character.Id, Sim.Capital, character.HomeOre, OrderSide.Sell))
        {
            return;
        }

        Credits carriedAsk = AskPrice(Sim.Capital, character.HomeOre, character, carried);

        _world.PlaceOrder(
            character,
            Sim.Capital,
            OrderSide.Sell,
            character.HomeOre,
            carriedAsk,
            Listable(character, Sim.Capital, carriedAsk, carried),
            day);
    }

    /// <summary>
    /// Buys all four planet-locked ores at the capital and builds alloy frames.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The only bot whose inputs cannot be sourced inside one faction: <c>build_alloy_frame</c>
    /// takes ten of each ore, and two of the four come from the other side. Every unit it buys from
    /// a planet across the line is a cross-faction trade, which is the demand ADR-0008 assumes
    /// exists.
    /// </para>
    /// <para>
    /// Bids at the capital only. A framewright could in principle fly to each of four homeworlds,
    /// but the capital is where all four ores are already on one book, and modelling the shortcut
    /// nobody would skip keeps the simulation about prices rather than about routing.
    /// </para>
    /// </remarks>
    private void RunFramewright(SimCharacter character, int day)
    {
        if (!CanReachCapital(character, day))
        {
            return;
        }

        int level = SkillCurve.LevelForXp(character.Xp.GetValueOrDefault("shipcrafting"));
        int slots = IndustrySlots.MaxConcurrentJobs(level);

        // How many frames the ore on hand supports: the scarcest of the four ores decides, which is
        // what makes a single missing supply line stop production rather than slow it.
        int materialLimited = Sim.PlanetLockedOres.Min(o => character.Held(o) / Sim.OrePerFrame);
        int runs = Math.Min(slots, materialLimited);

        if (runs > 0)
        {
            Credits fee = IndustryFees.ForJob(runs);

            if (character.Balance >= fee)
            {
                _world.Adjust(character, -fee, LedgerReason.IndustryFee);

                foreach (string ore in Sim.PlanetLockedOres)
                {
                    _world.RecordDestroyed(character, ore, runs * Sim.OrePerFrame);
                }

                _world.RecordCrafted(character, Sim.Frame, runs);

                character.Xp["shipcrafting"] =
                    character.Xp.GetValueOrDefault("shipcrafting") + (runs * 600L);
            }
        }

        if (character.Held(Sim.Frame) > 0
            && !_world.HasRestingOrder(character.Id, Sim.Capital, Sim.Frame, OrderSide.Sell))
        {
            int listing = character.Held(Sim.Frame);

            _world.PlaceOrder(
                character,
                Sim.Capital,
                OrderSide.Sell,
                Sim.Frame,
                AskPrice(Sim.Capital, Sim.Frame, character, listing),
                listing,
                day);
        }

        // Stop buying inputs for a product that is not selling.
        //
        // Without this the framewright restocked ore forever regardless of whether a single frame
        // had ever found a buyer, which quietly destroyed the negative control: cross-faction ore
        // kept flowing in a world where nothing consumed frames, so the invariant passed on the
        // exact economy it exists to catch. No player keeps buying materials while unsold stock
        // piles up, and a bot that does makes the check untrustworthy rather than the bot generous.
        // Held plus listed-but-unsold. Listing moves goods onto the order, so counting only what is
        // held reads a shelf full of frames nobody wants as a shelf that cleared.
        int unsold = character.Held(Sim.Frame)
            + _world.RestingQuantity(character.Id, Sim.Capital, Sim.Frame, OrderSide.Sell);

        if (unsold >= UnsoldFrameLimit)
        {
            return;
        }

        // Restock whichever ores are short, splitting the purse four ways so one expensive ore
        // cannot consume the budget and leave the frame permanently one ingredient away.
        Credits perOre = character.Balance.PercentRoundedDown(2_000);

        foreach (string ore in Sim.PlanetLockedOres)
        {
            if (_world.HasRestingOrder(character.Id, Sim.Capital, ore, OrderSide.Buy))
            {
                continue;
            }

            int shortfall = (slots * Sim.OrePerFrame) - character.Held(ore);

            if (shortfall <= 0)
            {
                continue;
            }

            Credits bid = _world.LastPrice(Sim.Capital, ore)
                .PercentRoundedUp(10_000 + SpreadBasisPoints);

            int affordable = Affordable(character, bid, shortfall, perOre);

            _world.PlaceOrder(character, Sim.Capital, OrderSide.Buy, ore, Max(bid), affordable, day);
        }
    }

    /// <summary>
    /// Buys inputs, runs manufacturing jobs, and lists the output.
    /// </summary>
    /// <remarks>
    /// Daily throughput is slots multiplied by how many times the job fits in a day, because
    /// industry jobs run while logged off. In practice materials bind long before time does, which
    /// is itself one of the more interesting things the simulation shows.
    /// </remarks>
    private void RunIndustry(
        SimCharacter character,
        int day,
        string skill,
        string input,
        int inputPerRun,
        string output,
        int outputPerRun,
        int jobSeconds,
        string inputMarket,
        string outputMarket)
    {
        int level = SkillCurve.LevelForXp(character.Xp.GetValueOrDefault(skill));
        int slots = IndustrySlots.MaxConcurrentJobs(level);

        int timeLimitedRuns = slots * (86_400 / jobSeconds);
        int materialLimitedRuns = character.Held(input) / inputPerRun;
        int runs = Math.Min(timeLimitedRuns, materialLimitedRuns);

        if (runs > 0)
        {
            Credits fee = IndustryFees.ForJob(runs);

            if (character.Balance >= fee)
            {
                _world.Adjust(character, -fee, LedgerReason.IndustryFee);

                _world.RecordDestroyed(character, input, runs * inputPerRun);
                _world.RecordCrafted(character, output, runs * outputPerRun);

                character.Xp[skill] = character.Xp.GetValueOrDefault(skill) + (runs * 600L);
            }
        }

        if (character.Held(output) > 0
            && !_world.HasRestingOrder(character.Id, outputMarket, output, OrderSide.Sell))
        {
            int listing = character.Held(output);
            Credits ask = AskPrice(outputMarket, output, character, listing);

            _world.PlaceOrder(
                character,
                outputMarket,
                OrderSide.Sell,
                output,
                ask,
                Listable(character, outputMarket, ask, listing),
                day);
        }

        // Restocking at the capital means waiting for a flight; at home it is every day.
        if (inputMarket == Sim.Capital && !CanReachCapital(character, day))
        {
            return;
        }

        // Restock: bid for enough input to keep the lines busy tomorrow.
        if (_world.HasRestingOrder(character.Id, inputMarket, input, OrderSide.Buy))
        {
            return;
        }

        int shortfall = (timeLimitedRuns * inputPerRun) - character.Held(input);

        if (shortfall <= 0)
        {
            return;
        }

        Credits bid = _world.LastPrice(inputMarket, input)
            .PercentRoundedUp(10_000 + SpreadBasisPoints);

        // Capped so a refiner does not escrow its entire purse against a day of theoretical
        // throughput it has never once achieved.
        int affordable = Affordable(character, bid, shortfall, character.Balance.PercentRoundedDown(5_000));

        _world.PlaceOrder(character, inputMarket, OrderSide.Buy, input, Max(bid), affordable, day);
    }

    /// <summary>
    /// Flies: keeps a hull section on hand, loses it sometimes, and buys another.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The consumer at the end of the chain. A pilot produces nothing and sells nothing — it turns
    /// quest income into demand for the one finished good, and destroys what it buys. Without this
    /// the market has no terminal customer and the whole chain's revenue collapses to the price
    /// floor, which is precisely what the first EconSim run recorded.
    /// </para>
    /// <para>
    /// A pilot bids only when it has no ship and nothing already resting, so demand is one order
    /// per loss rather than a standing bid that quietly accumulates hulls it will never fly.
    /// </para>
    /// </remarks>
    private void RunPilot(SimCharacter character, int day, ref SplitMix64 rng)
    {
        // The frame is bought at the capital, because that is where it is built. A pilot flying one
        // is the terminal demand the shipped content is missing.
        if (_config.PilotsFlyFrameHulls)
        {
            RunPilotFor(character, day, Sim.Frame, Sim.Capital, ref rng);
        }

        RunPilotFor(character, day, Sim.Section, character.HomeBody, ref rng);
    }

    private void RunPilotFor(
        SimCharacter character, int day, string hull, string market, ref SplitMix64 rng)
    {
        if (hull == Sim.Frame && !CanReachCapital(character, day))
        {
            return;
        }

        if (character.Held(hull) > 0)
        {
            // Flying risks the ship. Destroyed outright rather than damaged: this simulation is
            // measuring demand, and a repair economy is a different question from a replacement one.
            if (rng.NextBelow(10_000) < (int)(_config.PilotLossChance * 10_000))
            {
                _world.RecordDestroyed(character, hull, 1);
            }

            return;
        }

        if (_world.HasRestingOrder(character.Id, market, hull, OrderSide.Buy))
        {
            return;
        }

        // Bids above the last trade, because a grounded pilot wants a ship more than it wants a
        // bargain. That willingness to pay up is what gives the chain its margin.
        Credits bid = _world.LastPrice(market, hull)
            .PercentRoundedUp(10_000 + SpreadBasisPoints);

        int quantity = Affordable(character, Max(bid), 1);

        if (quantity > 0)
        {
            _world.PlaceOrder(character, market, OrderSide.Buy, hull, Max(bid), quantity, day);
        }
    }

    /// <summary>
    /// Provides liquidity: bids under the last price and asks above it.
    /// </summary>
    private void RunTrader(SimCharacter character, int day, ref SplitMix64 rng)
    {
        string item = Sim.TradedItems[rng.NextBelow(Sim.TradedItems.Length)];

        // Traders work the capital when they can reach it and their home book otherwise, so the
        // liquidity they provide is spread across venues rather than concentrated wherever the
        // spread happened to be widest on day one.
        string market = CanReachCapital(character, day) ? Sim.Capital : character.HomeBody;

        if (_world.HasRestingOrder(character.Id, market, item, OrderSide.Buy)
            || _world.HasRestingOrder(character.Id, market, item, OrderSide.Sell))
        {
            return;
        }

        int held = character.Held(item);

        if (held > 0)
        {
            Credits ask = _world.LastPrice(market, item).PercentRoundedUp(10_000 + SpreadBasisPoints);
            _world.PlaceOrder(character, market, OrderSide.Sell, item, Max(ask), held, day);

            return;
        }

        Credits bid = _world.LastPrice(market, item).PercentRoundedDown(10_000 - SpreadBasisPoints);

        // A tenth of the purse per position, so one bad fill cannot take a trader out of the market.
        Credits budget = character.Balance.PercentRoundedDown(1_000);
        int quantity = Affordable(character, bid, int.MaxValue, budget);

        _world.PlaceOrder(character, market, OrderSide.Buy, item, Max(bid), quantity, day);
    }

    /// <summary>
    /// Destroys a fraction of manufactured goods, standing in for combat losses.
    /// </summary>
    /// <remarks>
    /// The material sink the game does not have yet. Left at zero by default so the simulation
    /// reports what is actually built rather than what is planned.
    /// </remarks>
    private void ApplyLosses(ref SplitMix64 rng)
    {
        if (_config.DailyLossRate <= 0)
        {
            return;
        }

        foreach (SimCharacter character in _world.Characters)
        {
            foreach (string item in new[] { Sim.Plate, Sim.Section })
            {
                int held = character.Held(item);

                if (held == 0)
                {
                    continue;
                }

                int lost = (int)(held * _config.DailyLossRate);

                // Give fractional losses a proportional chance, so small holdings still decay.
                if (lost == 0 && rng.NextBelow(10_000) < (int)(_config.DailyLossRate * 10_000))
                {
                    lost = 1;
                }

                if (lost > 0)
                {
                    _world.RecordDestroyed(character, item, Math.Min(lost, held));
                }
            }
        }
    }

    /// <summary>
    /// How many units a seller can afford to <em>list</em>, given the broker fee.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The broker fee is a percentage of order value and is charged on placement, so listing a
    /// large stock costs real credits before a single unit sells. A miner sitting on thousands of
    /// units of ore owes thousands of credits to offer them, and <see cref="MarketFees"/> waives at
    /// most one credit — deliberately, because a waiver that scales with order value would be
    /// farmed.
    /// </para>
    /// <para>
    /// Without this the whole supply chain silently stopped. Miners accumulated ore they could not
    /// afford to sell, the ore never reached the capital, framewrights never got inputs, and
    /// cross-faction trade died — all of it looking like weak demand rather than a seller locked
    /// out of the book. A real player in that position lists what they can and comes back for the
    /// rest, which is what this does.
    /// </para>
    /// </remarks>
    private int Listable(SimCharacter seller, string market, Credits unitPrice, int wanted)
    {
        if (!unitPrice.IsPositive || wanted <= 0)
        {
            return 0;
        }

        long basisPoints = MarketFees.DefaultBrokerFeeBasisPoints;

        if (market == Sim.Capital)
        {
            // Mirrors the premium SimWorld.PlaceOrder applies, so the two cannot disagree about
            // what an order costs.
            basisPoints += basisPoints * _config.CapitalFeePremiumBasisPoints / 10_000;
        }

        // Largest q with ceil(price * q * bp / 10000) <= balance.
        long affordable = seller.Balance.MinorUnits * 10_000 / (unitPrice.MinorUnits * basisPoints);

        return (int)Math.Clamp(affordable, 0, wanted);
    }

    /// <summary>How many units a character can afford at a price, including the broker fee.</summary>
    private static int Affordable(
        SimCharacter character, Credits unitPrice, int wanted, Credits? budgetOverride = null)
    {
        if (!unitPrice.IsPositive)
        {
            return 0;
        }

        Credits budget = budgetOverride ?? character.Balance;

        // The fee is a percentage on top, so leave headroom rather than discovering the shortfall
        // at placement and having the order silently dropped.
        long perUnit = unitPrice.MinorUnits
            + Math.Max(1, unitPrice.MinorUnits * MarketFees.DefaultBrokerFeeBasisPoints / 10_000);

        long affordable = budget.MinorUnits / perUnit;

        return (int)Math.Clamp(affordable, 0, wanted);
    }

    /// <summary>
    /// What a seller asks, given how well their stock has been moving.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Two-sided, and it has to be. Sellers undercutting each other is how a price falls, but an
    /// ask rule that <em>only</em> undercuts can only ever ratchet downwards: it walks every market
    /// to the price floor and leaves it there permanently, no matter how strong demand becomes.
    /// The first EconSim run read that as an economy with no material sink. It was really an
    /// economy with no way to express scarcity.
    /// </para>
    /// <para>
    /// The signal is unsold stock. A seller sitting on a backlog undercuts to move it; a seller
    /// whose goods clear as fast as they are listed asks for more. That is the feedback loop that
    /// lets a price discover a level instead of sliding to zero.
    /// </para>
    /// </remarks>
    /// <param name="listing">Units about to be listed, as the yardstick for "a normal day's stock".</param>
    private Credits AskPrice(string market, string item, SimCharacter seller, int listing)
    {
        // Backlog means more than a day's worth already sitting unsold behind this listing.
        bool backlog = seller.Held(item) > listing;

        if (backlog)
        {
            Credits floor = _world.BestAsk(market, item) ?? _world.LastPrice(market, item);

            return Max(floor.PercentRoundedDown(10_000 - SpreadBasisPoints));
        }

        return Max(_world.LastPrice(market, item).PercentRoundedUp(10_000 + SpreadBasisPoints));
    }

    /// <summary>Keeps a price from reaching zero, which would make the book meaningless.</summary>
    private static Credits Max(Credits price) =>
        price.IsPositive ? price : Credits.FromMinorUnits(1);
}
