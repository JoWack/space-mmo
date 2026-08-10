using System.Globalization;
using System.Text;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Random;
using SpaceMMO.EconSim;

// Headless economy simulator. See economy-design §4 for the invariants it checks.
//
//   dotnet run --project tools/SpaceMMO.EconSim -- [days] [csvPath]

if (args.Length > 0 && args[0] == "--sweep")
{
    if (args.Length > 1 && args[1] == "population")
    {
        RunPopulationSweep();
    }
    else if (args.Length > 1 && args[1] == "capital")
    {
        RunCapitalSweep();
    }
    else if (args.Length > 1 && args[1] == "loss")
    {
        RunLossSweep();
    }
    else
    {
        RunSweep();
    }

    return 0;
}

int days = args.Length > 0 && int.TryParse(args[0], CultureInfo.InvariantCulture, out int parsed)
    ? parsed
    : 3_650;

var config = new SimulationConfig
{
    Days = days,
    DailyQuestCredits = args.Length > 1
        && int.TryParse(args[1], CultureInfo.InvariantCulture, out int faucet)
            ? Credits.FromWholeCredits(faucet)
            : Credits.Zero,
    CsvPath = args.Length > 2 ? args[2] : null,

    // On unless switched off, matching authored content now that assemble_hull_freighter consumes
    // frames. --no-frame-hulls reproduces the economy from before that recipe existed, which is
    // what makes the cross-faction invariant demonstrably able to fail as well as pass.
    PilotsFlyFrameHulls = !args.Contains("--no-frame-hulls"),
};

var world = new SimWorld(config);
var bots = new Bots(config, world);
var rng = new SplitMix64(config.Seed);

var violations = new List<InvariantViolation>();
var rows = new List<string>
{
    "day,money_supply_cr,escrow_cr,ore_held,plate_held,section_held,ore_price_cr,plate_price_cr,section_price_cr,trades",
};

Console.WriteLine(
    $"Simulating {config.Days:N0} days with {config.TotalBots} bots "
    + $"({config.Miners} miners, {config.Refiners} refiners, {config.Crafters} crafters, "
    + $"{config.Framewrights} framewrights, {config.Traders} traders, {config.Pilots} pilots) "
    + "across four homeworlds and the capital...");
Console.WriteLine();

int tradesAtLastDay = 0;

for (int day = 1; day <= config.Days; day++)
{
    bots.RunDay(day, ref rng);

    // Every day, not just at the end: a conservation break needs to be caught on the day it
    // happens, or the cause is buried under a decade of subsequent transactions.
    List<InvariantViolation> today = Invariants.Check(world, day);

    if (today.Count > 0 && violations.Count < 20)
    {
        violations.AddRange(today);
    }

    if (config.CsvPath is not null || day == config.Days)
    {
        rows.Add(string.Create(
            CultureInfo.InvariantCulture,
            $"{day},{Whole(MoneySupply(world))},{Whole(world.Escrow)},"
            + $"{Held(world, Sim.Ore)},{Held(world, Sim.Plate)},{Held(world, Sim.Section)},"
            + $"{Whole(LastTraded(world, Sim.Ore))},{Whole(LastTraded(world, Sim.Plate))},"
            + $"{Whole(LastTraded(world, Sim.Section))},{world.Trades.Count - tradesAtLastDay}"));
    }

    tradesAtLastDay = world.Trades.Count;
}

Report(world, config, violations);

if (config.CsvPath is not null)
{
    await File.WriteAllLinesAsync(config.CsvPath, rows, Encoding.UTF8);
    Console.WriteLine($"Wrote {rows.Count - 1:N0} rows to {config.CsvPath}");
}

return violations.Count == 0 ? 0 : 1;

/// <summary>
/// Sweeps the steady-state faucet to find where the money supply stops shrinking.
/// </summary>
/// <remarks>
/// This is the parameter search economy-design §2b describes: sinks scale with activity, so there
/// is some daily credit grant at which <c>F ≈ S</c> and the supply holds steady. Guessing it is
/// how economies end up broken; measuring it is why this tool exists.
/// </remarks>
static void RunSweep()
{
    Console.WriteLine("Sweeping the daily quest faucet against a 5-year run.");
    Console.WriteLine();
    Console.WriteLine(
        $"  {"cr/day",8} {"supply (cr)",16} {"vs bootstrap",14} {"trades",12} {"ore traded",14}");
    Console.WriteLine(new string('─', 70));

    foreach (int perDay in new[] { 0, 5, 10, 25, 50, 100, 250 })
    {
        var sweepConfig = new SimulationConfig
        {
            Days = 1_825,
            DailyQuestCredits = Credits.FromWholeCredits(perDay),
        };

        var sweepWorld = new SimWorld(sweepConfig);
        var sweepBots = new Bots(sweepConfig, sweepWorld);
        var sweepRng = new SplitMix64(sweepConfig.Seed);

        for (int day = 1; day <= sweepConfig.Days; day++)
        {
            sweepBots.RunDay(day, ref sweepRng);
        }

        Credits supply = MoneySupply(sweepWorld);
        Credits bootstrap = sweepConfig.BootstrapCredits * sweepConfig.TotalBots;

        double ratio = 100.0 * supply.MinorUnits / bootstrap.MinorUnits;
        long oreTraded = sweepWorld.Trades.Where(t => t.Item == Sim.Ore).Sum(t => (long)t.Quantity);

        Console.WriteLine(
            $"  {perDay,8:N0} {Whole(supply),16:N0} {ratio,13:N1}% "
            + $"{sweepWorld.Trades.Count,12:N0} {oreTraded,14:N0}");
    }

    Console.WriteLine();
    Console.WriteLine("A supply holding near 100% of bootstrap is the equilibrium F ≈ S.");
}

/// <summary>
/// Sweeps the material sink to find the loss rate at which manufactured goods stop accumulating.
/// </summary>
/// <remarks>
/// <para>
/// The companion to <see cref="RunSweep"/>, and the answer to the worst finding in §5a: ore was
/// created four orders of magnitude faster than it was consumed, and its price collapsed to zero.
/// The faucet sweep asks how many credits must enter per day; this asks how much <em>matter</em>
/// must leave.
/// </para>
/// <para>
/// Losses fall on plates and hull sections rather than on ore, which is the honest model — nobody
/// blows up a pile of raw ore. Ore is consumed <em>indirectly</em>, by the refining and crafting
/// that replaces what was destroyed, so the ore market is pulled by demand rather than by a rule
/// that deletes ore. That the ore price responds at all is therefore the finding, not a given.
/// </para>
/// </remarks>
static void RunLossSweep()
{
    Console.WriteLine("Sweeping the daily material loss rate against a 5-year run.");
    Console.WriteLine("Losses apply to plates and hull sections; ore is consumed by replacing them.");
    Console.WriteLine();
    Console.WriteLine(
        $"  {"loss/day",9} {"ore created",14} {"ore consumed",14} {"ore held",14} "
        + $"{"ore price",11} {"section price",14}");
    Console.WriteLine(new string('─', 88));

    foreach (double rate in new[] { 0.0, 0.001, 0.005, 0.01, 0.025, 0.05, 0.10, 0.25 })
    {
        var sweepConfig = new SimulationConfig
        {
            Days = 1_825,
            // Held at the measured equilibrium so the material question is asked of an economy
            // whose money supply is stable. Sweeping a material rate against a collapsing credit
            // supply would confound the two and answer neither.
            DailyQuestCredits = Credits.FromWholeCredits(50),
            DailyLossRate = rate,
        };

        var sweepWorld = new SimWorld(sweepConfig);
        var sweepBots = new Bots(sweepConfig, sweepWorld);
        var sweepRng = new SplitMix64(sweepConfig.Seed);

        for (int day = 1; day <= sweepConfig.Days; day++)
        {
            sweepBots.RunDay(day, ref sweepRng);
        }

        long oreCreated = sweepWorld.Gathered.GetValueOrDefault(Sim.Ore);
        long oreHeld = Held(sweepWorld, Sim.Ore);

        Console.WriteLine(
            $"  {rate,9:P1} {oreCreated,14:N0} {oreCreated - oreHeld,14:N0} {oreHeld,14:N0} "
            + $"{Money(LastTraded(sweepWorld, Sim.Ore)),11} "
            + $"{Money(LastTraded(sweepWorld, Sim.Section)),14}");
    }

    Console.WriteLine();
    Console.WriteLine("Ore stops accumulating where consumption approaches creation.");
}

/// <summary>
/// Sweeps the capital's fee premium against flight time, looking for the setting where four
/// homeworld books stay alive and cross-faction trade still happens.
/// </summary>
/// <remarks>
/// <para>
/// The two are one trade-off, which is why they are swept together rather than one at a time. The
/// capital is the only venue carrying all four planet-locked ores, so it is strictly more useful
/// than any homeworld. Left free and close, everybody lists there and the four local books die,
/// which makes four planets scenery. Priced or distanced too far, nobody goes, the ores never
/// meet, and ADR-0008's contested zone has nothing to contest. Both failures are quiet.
/// </para>
/// <para>
/// The number to watch is the last column against the second-to-last: local trade has to survive
/// without cross-faction trade going to zero. Either one alone can be maximised by breaking the
/// other.
/// </para>
/// </remarks>
static void RunCapitalSweep()
{
    Console.WriteLine("Sweeping the capital's fee premium against flight time, 5-year runs.");
    Console.WriteLine("Faucet held at the measured equilibrium so only these two vary.");
    Console.WriteLine();
    Console.WriteLine(
        $"  {"premium",8} {"trip",5} {"frames",8} {"x-faction ore",14} "
        + $"{"local/100d",11} {"capital/100d",13}");
    Console.WriteLine(new string('─', 66));

    foreach (int premium in new[] { 0, 2_500, 5_000, 10_000, 25_000 })
    {
        foreach (int trip in new[] { 1, 3, 7 })
        {
            var sweepConfig = new SimulationConfig
            {
                Days = 1_825,
                DailyQuestCredits = Credits.FromWholeCredits(50),
                CapitalFeePremiumBasisPoints = premium,
                CapitalTripDays = trip,
            };

            var sweepWorld = new SimWorld(sweepConfig);
            var sweepBots = new Bots(sweepConfig, sweepWorld);
            var sweepRng = new SplitMix64(sweepConfig.Seed);

            for (int day = 1; day <= sweepConfig.Days; day++)
            {
                sweepBots.RunDay(day, ref sweepRng);
            }

            long crossOre = sweepWorld.Trades
                .Where(t => t.IsCrossFaction && Sim.PlanetLockedOres.Contains(t.Item))
                .Sum(t => (long)t.Quantity);

            int recentLocal = sweepWorld.Trades
                .Count(t => t.Day > sweepConfig.Days - 100 && t.Market != Sim.Capital);

            int recentCapital = sweepWorld.Trades
                .Count(t => t.Day > sweepConfig.Days - 100 && t.Market == Sim.Capital);

            Console.WriteLine(
                $"  {premium,8:N0} {trip,5} {sweepWorld.Crafted.GetValueOrDefault(Sim.Frame),8:N0} "
                + $"{crossOre,14:N0} {recentLocal,11:N0} {recentCapital,13:N0}");
        }
    }

    Console.WriteLine();
    Console.WriteLine("Want local trade alive AND cross-faction ore well above zero.");
    Console.WriteLine();
    Console.WriteLine("trip=1 is degenerate, not a finding: a miner works the capital's ferrite");
    Console.WriteLine("when it is there and its homeworld's ore when it is not, so a trip length");
    Console.WriteLine("of one day means it is never home and no planet-locked ore is ever mined.");
    Console.WriteLine("Read trip 3 against trip 7; ignore the zeroes above them.");
}

/// <summary>
/// Sweeps how many of the population produce raw material against how many consume finished
/// goods, holding the total roughly constant.
/// </summary>
/// <remarks>
/// <para>
/// The question left open by #90. Ore is created about four orders of magnitude faster than it is
/// consumed and sits at the price floor, and the obvious reading is that deposits are too generous.
/// The arithmetic says otherwise: pilots are the only terminal consumers, twenty-five of them lose
/// roughly 1.25 hull sections a day, and a hull section is four plates or eighty ferrite. That is
/// about a hundred units of real demand a day against a ceiling of 28,800. No node capacity fixes
/// a ratio; the number of people mining against the number of people buying does.
/// </para>
/// <para>
/// Which matters because the two have opposite consequences. If the price only recovers at a
/// miner-to-pilot ratio no real game would have, the finding is that this bot mix is
/// unrepresentative and the content is fine. If it recovers at a plausible one, the deposits really
/// are too rich and data/universe/origin.json needs changing. Tuning the nodes without asking would
/// have produced a healthy-looking chart either way.
/// </para>
/// </remarks>
static void RunPopulationSweep()
{
    Console.WriteLine("Sweeping producers against consumers, 5-year runs, total population held near 100.");
    Console.WriteLine("Faucet at the measured equilibrium; deposits as authored.");
    Console.WriteLine();
    Console.WriteLine(
        $"  {"miners",7} {"pilots",7} {"ratio",7} {"ferrite price",14} {"ore created",13} "
        + $"{"ore used",11} {"frames",7}");
    Console.WriteLine(new string('─', 78));

    foreach ((int miners, int pilots) in new[]
    {
        (40, 25), (30, 40), (20, 55), (12, 65), (8, 70), (4, 75),
    })
    {
        var sweepConfig = new SimulationConfig
        {
            Days = 1_825,
            DailyQuestCredits = Credits.FromWholeCredits(50),
            Miners = miners,
            Pilots = pilots,
        };

        var sweepWorld = new SimWorld(sweepConfig);
        var sweepBots = new Bots(sweepConfig, sweepWorld);
        var sweepRng = new SplitMix64(sweepConfig.Seed);

        for (int day = 1; day <= sweepConfig.Days; day++)
        {
            sweepBots.RunDay(day, ref sweepRng);
        }

        long created = sweepWorld.Gathered.GetValueOrDefault(Sim.Ore);
        long used = sweepWorld.Destroyed.GetValueOrDefault(Sim.Ore);

        Console.WriteLine(
            $"  {miners,7} {pilots,7} {(double)miners / pilots,7:N2} "
            + $"{Money(LastTraded(sweepWorld, Sim.Ore)),14} {created,13:N0} {used,11:N0} "
            + $"{sweepWorld.Crafted.GetValueOrDefault(Sim.Frame),7:N0}");
    }

    Console.WriteLine();
    Console.WriteLine("A price off the 0.01 floor means supply and demand are finally comparable.");
}

static Credits MoneySupply(SimWorld world) =>
    Credits.FromMinorUnits(world.Characters.Sum(c => c.Balance.MinorUnits)) + world.Escrow;

static long Whole(Credits credits) => credits.MinorUnits / Credits.MinorUnitsPerCredit;

/// <summary>
/// Formats a price with its minor units intact.
/// </summary>
/// <remarks>
/// Prices are floored at one minor unit, never at zero. Printing them through <c>Whole</c>
/// integer-divided that floor down to "0 cr" and made a collapsed-but-live market look like a
/// dead one — the sub-credit range is exactly where a collapsing price spends its time, so it is
/// the one range the report must not round away.
/// </remarks>
static string Money(Credits credits) => string.Create(
    CultureInfo.InvariantCulture,
    $"{credits.MinorUnits / (decimal)Credits.MinorUnitsPerCredit:N2}");

static long Held(SimWorld world, string item) =>
    world.Characters.Sum(c => (long)c.Held(item)) + world.GoodsOnBook(item);

/// <summary>
/// The most recent price an item traded at, anywhere.
/// </summary>
/// <remarks>
/// Prices are per market now, so there is no single "the" price. For a summary line the last trade
/// anywhere is the honest answer: it is a real transaction rather than an average of venues that
/// never traded with each other.
/// </remarks>
static Credits LastTraded(SimWorld world, string item)
{
    for (int i = world.Trades.Count - 1; i >= 0; i--)
    {
        if (world.Trades[i].Item == item)
        {
            return world.Trades[i].Price;
        }
    }

    return Credits.Zero;
}

static void Report(SimWorld world, SimulationConfig config, List<InvariantViolation> violations)
{
    Console.WriteLine("── Invariants ─────────────────────────────────────────────");

    if (violations.Count == 0)
    {
        Console.WriteLine("  All conservation invariants held for every simulated day.");
    }
    else
    {
        Console.WriteLine($"  {violations.Count} violation(s):");

        foreach (InvariantViolation violation in violations.Take(10))
        {
            Console.WriteLine($"    {violation}");
        }
    }

    Console.WriteLine();
    Console.WriteLine("── Money supply ───────────────────────────────────────────");

    long faucets = 0;
    long sinks = 0;
    var bySink = new Dictionary<LedgerReason, long>();

    foreach ((_, Credits delta, LedgerReason reason) in world.Ledger)
    {
        switch (LedgerReasons.KindOf(reason))
        {
            case LedgerReasonKind.Faucet:
                faucets += delta.MinorUnits;
                break;

            case LedgerReasonKind.Sink:
                sinks += -delta.MinorUnits;
                bySink[reason] = bySink.GetValueOrDefault(reason) + -delta.MinorUnits;
                break;
        }
    }

    Credits supply = MoneySupply(world);
    Credits bootstrap = config.BootstrapCredits * config.TotalBots;

    Console.WriteLine($"  Created by faucets   {Whole(Credits.FromMinorUnits(faucets)),15:N0} cr");
    Console.WriteLine($"  Destroyed by sinks   {Whole(Credits.FromMinorUnits(sinks)),15:N0} cr");

    foreach ((LedgerReason reason, long amount) in bySink.OrderByDescending(e => e.Value))
    {
        Console.WriteLine(
            $"    {reason,-20} {Whole(Credits.FromMinorUnits(amount)),13:N0} cr");
    }

    Console.WriteLine($"  Remaining supply     {Whole(supply),15:N0} cr");

    double retained = bootstrap.MinorUnits == 0
        ? 0
        : 100.0 * supply.MinorUnits / bootstrap.MinorUnits;

    Console.WriteLine(
        $"  Retained             {retained,15:N1} % of the {Whole(bootstrap):N0} cr ever created");

    Console.WriteLine();
    Console.WriteLine("── Material ───────────────────────────────────────────────");
    Console.WriteLine(
        $"  Deposit ceiling      {world.DailyCapacityFor(Sim.Ore),15:N0} ferrite/day, "
        + $"{world.DailyCapacityFor(Sim.TerranFerrite):N0} per planet-locked ore/day");

    foreach (string item in Sim.TradedItems)
    {
        long created = world.Gathered.GetValueOrDefault(item) + world.Crafted.GetValueOrDefault(item);
        long destroyed = world.Destroyed.GetValueOrDefault(item);

        Console.WriteLine(
            $"  {item,-20} created {created,14:N0}  destroyed {destroyed,14:N0}  "
            + $"held {Held(world, item),14:N0}");
    }

    Console.WriteLine();
    Console.WriteLine("── Prices ─────────────────────────────────────────────────");

    foreach (string item in Sim.TradedItems)
    {
        List<SimTrade> recent = world.Trades
            .Where(t => t.Item == item && t.Day > config.Days - 100)
            .ToList();

        string window = recent.Count == 0
            ? "no trades in the last 100 days"
            : $"last-100-day mean {Money(Mean(recent)),8} cr over {recent.Count,7:N0} trades";

        Console.WriteLine($"  {item,-20} {Money(LastTraded(world, item)),8} cr   {window}");
    }

    Console.WriteLine();
    Console.WriteLine("── Markets ────────────────────────────────────────────────");
    Console.WriteLine("  A homeworld book that never trades is a planet nobody needs to visit.");
    Console.WriteLine();

    foreach (string market in
        new[] { "body_terra", "body_ares", "body_verdance", "body_grimhold", Sim.Capital })
    {
        List<SimTrade> here = world.Trades
            .Where(t => t.Market == market && t.Day > config.Days - 100)
            .ToList();

        Console.WriteLine(
            $"  {market,-16} {here.Count,10:N0} trades in the last 100 days, "
            + $"{world.OrdersAt(market),6:N0} orders resting");
    }

    Console.WriteLine();
    Console.WriteLine("── Cross-faction demand ───────────────────────────────────");

    long crossOre = world.Trades
        .Where(t => t.IsCrossFaction && Sim.PlanetLockedOres.Contains(t.Item))
        .Sum(t => (long)t.Quantity);

    long allOre = world.Trades
        .Where(t => Sim.PlanetLockedOres.Contains(t.Item))
        .Sum(t => (long)t.Quantity);

    double share = allOre == 0 ? 0 : 100.0 * crossOre / allOre;

    Console.WriteLine($"  Alloy frames built   {world.Crafted.GetValueOrDefault(Sim.Frame),15:N0}");
    Console.WriteLine($"  Planet-locked ore    {allOre,15:N0} units traded");
    Console.WriteLine($"  ... across factions  {crossOre,15:N0} units ({share:N1}%)");

    foreach (InvariantViolation violation in Invariants.CheckCrossFactionDemand(world, config.Days))
    {
        Console.WriteLine($"  VIOLATION: {violation}");
    }

    Console.WriteLine();
    Console.WriteLine("── Progression ────────────────────────────────────────────");

    foreach (IGrouping<string, SimCharacter> group in world.Characters.GroupBy(c => c.Archetype))
    {
        SimCharacter sample = group.First();

        string skill = "none";
        long xp = 0;

        if (sample.Xp.Count > 0)
        {
            KeyValuePair<string, long> best = sample.Xp.MaxBy(e => e.Value);
            skill = best.Key;
            xp = best.Value;
        }

        int level = xp > 0 ? SkillCurve.LevelForXp(xp) : 1;

        Console.WriteLine(
            $"  {group.Key,-10} n={group.Count(),-4} best skill {skill,-14} "
            + $"level {level,2} ({xp,14:N0} xp)");
    }

    Console.WriteLine();
}

static Credits Mean(List<SimTrade> trades)
{
    long units = trades.Sum(t => (long)t.Quantity);

    if (units == 0)
    {
        return Credits.Zero;
    }

    long value = trades.Sum(t => t.Price.MinorUnits * t.Quantity);

    return Credits.FromMinorUnits(value / units);
}
