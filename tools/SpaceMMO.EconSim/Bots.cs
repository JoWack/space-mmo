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

    private readonly SimulationConfig _config = config;
    private readonly SimWorld _world = world;

    /// <summary>Ore still available from the shared deposits today.</summary>
    private int _oreLeftToday;

    public void RunDay(int day, ref SplitMix64 rng)
    {
        _oreLeftToday = _world.DailyNodeCapacity;

        _world.ExpireOrders(day);

        foreach (SimCharacter character in _world.Characters)
        {
            PayDailyQuestCredits(character);

            switch (character.Archetype)
            {
                case "miner":
                    RunMiner(character, day);
                    break;

                case "refiner":
                    RunIndustry(character, day, "refining", Sim.Ore, 20, Sim.Plate, 4, 60);
                    break;

                case "crafter":
                    RunIndustry(character, day, "shipcrafting", Sim.Plate, 4, Sim.Section, 1, 300);
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
        int level = SkillCurve.LevelForXp(character.Xp.GetValueOrDefault("mining"));

        // A day of continuous play, not a single call. GatheringYield.UnitsAvailable applies the
        // 20-tick banking cap, which exists to stop one call claiming an hour of idle time — using
        // it here would model a player who gathers once and logs off, and understated raw material
        // supply roughly fortyfold.
        long ticks = _config.ActiveSecondsPerDay / GatheringYield.TickSeconds;
        long capable = ticks * GatheringYield.UnitsPerTick(level);

        int wanted = (int)Math.Min(capable, _oreLeftToday);

        if (wanted <= 0)
        {
            return;
        }

        _oreLeftToday -= wanted;

        _world.RecordGathered(character, Sim.Ore, wanted);
        character.Xp["mining"] = character.Xp.GetValueOrDefault("mining")
            + (wanted * GatheringYield.XpPerUnit);

        // Miners are not traders: they list the day's output at a slight discount to move it.
        //
        // Listing *today's production* each day, rather than skipping the listing whenever an
        // order is already resting. The earlier rule looked like fee discipline but modelled a
        // miner who lists once and then hoards forever: a single stale order sat on the book while
        // the stock behind it grew without bound. That starved the ore market no matter how much
        // was mined, and it — not the game's balance — is what drove the price to zero in §5a.
        _world.PlaceOrder(
            character, OrderSide.Sell, Sim.Ore, AskPrice(Sim.Ore, character, wanted), wanted, day);
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
        int jobSeconds)
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
            && !_world.HasRestingOrder(character.Id, output, OrderSide.Sell))
        {
            int listing = character.Held(output);

            _world.PlaceOrder(
                character, OrderSide.Sell, output, AskPrice(output, character, listing), listing, day);
        }

        // Restock: bid for enough input to keep the lines busy tomorrow.
        if (_world.HasRestingOrder(character.Id, input, OrderSide.Buy))
        {
            return;
        }

        int shortfall = (timeLimitedRuns * inputPerRun) - character.Held(input);

        if (shortfall <= 0)
        {
            return;
        }

        Credits bid = _world.LastPrice[input].PercentRoundedUp(10_000 + SpreadBasisPoints);

        // Capped so a refiner does not escrow its entire purse against a day of theoretical
        // throughput it has never once achieved.
        int affordable = Affordable(character, bid, shortfall, character.Balance.PercentRoundedDown(5_000));

        _world.PlaceOrder(character, OrderSide.Buy, input, Max(bid), affordable, day);
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
        if (character.Held(Sim.Section) > 0)
        {
            // Flying risks the ship. Destroyed outright rather than damaged: this simulation is
            // measuring demand, and a repair economy is a different question from a replacement one.
            if (rng.NextBelow(10_000) < (int)(_config.PilotLossChance * 10_000))
            {
                _world.RecordDestroyed(character, Sim.Section, 1);
            }

            return;
        }

        if (_world.HasRestingOrder(character.Id, Sim.Section, OrderSide.Buy))
        {
            return;
        }

        // Bids above the last trade, because a grounded pilot wants a ship more than it wants a
        // bargain. That willingness to pay up is what gives the chain its margin.
        Credits bid = _world.LastPrice[Sim.Section].PercentRoundedUp(10_000 + SpreadBasisPoints);

        int quantity = Affordable(character, Max(bid), 1);

        if (quantity > 0)
        {
            _world.PlaceOrder(character, OrderSide.Buy, Sim.Section, Max(bid), quantity, day);
        }
    }

    /// <summary>
    /// Provides liquidity: bids under the last price and asks above it.
    /// </summary>
    private void RunTrader(SimCharacter character, int day, ref SplitMix64 rng)
    {
        string item = Sim.TradedItems[rng.NextBelow(Sim.TradedItems.Length)];

        if (_world.HasRestingOrder(character.Id, item, OrderSide.Buy)
            || _world.HasRestingOrder(character.Id, item, OrderSide.Sell))
        {
            return;
        }

        int held = character.Held(item);

        if (held > 0)
        {
            Credits ask = _world.LastPrice[item].PercentRoundedUp(10_000 + SpreadBasisPoints);
            _world.PlaceOrder(character, OrderSide.Sell, item, Max(ask), held, day);

            return;
        }

        Credits bid = _world.LastPrice[item].PercentRoundedDown(10_000 - SpreadBasisPoints);

        // A tenth of the purse per position, so one bad fill cannot take a trader out of the market.
        Credits budget = character.Balance.PercentRoundedDown(1_000);
        int quantity = Affordable(character, bid, int.MaxValue, budget);

        _world.PlaceOrder(character, OrderSide.Buy, item, Max(bid), quantity, day);
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
    private Credits AskPrice(string item, SimCharacter seller, int listing)
    {
        // Backlog means more than a day's worth already sitting unsold behind this listing.
        bool backlog = seller.Held(item) > listing;

        if (backlog)
        {
            Credits floor = _world.BestAsk(item) ?? _world.LastPrice[item];

            return Max(floor.PercentRoundedDown(10_000 - SpreadBasisPoints));
        }

        return Max(_world.LastPrice[item].PercentRoundedUp(10_000 + SpreadBasisPoints));
    }

    /// <summary>Keeps a price from reaching zero, which would make the book meaningless.</summary>
    private static Credits Max(Credits price) =>
        price.IsPositive ? price : Credits.FromMinorUnits(1);
}
