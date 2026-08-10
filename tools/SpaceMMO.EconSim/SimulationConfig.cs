using SpaceMMO.Domain.Economy;

namespace SpaceMMO.EconSim;

/// <summary>
/// Everything the simulation is allowed to vary.
/// </summary>
/// <remarks>
/// Kept in one place so a balance question becomes a parameter sweep rather than a code edit.
/// Defaults mirror the shipped content in <c>data/</c> and the first-draft rates in the design
/// bible, so a run with no overrides answers "is the game as designed balanced?".
/// </remarks>
public sealed record SimulationConfig
{
    /// <summary>How many days to simulate.</summary>
    public int Days { get; init; } = 3_650;

    public int Miners { get; init; } = 40;

    public int Refiners { get; init; } = 15;

    public int Crafters { get; init; } = 8;

    /// <summary>
    /// Bots that build alloy frames, and so must buy ore from the other faction.
    /// </summary>
    /// <remarks>
    /// The only archetype whose recipe cannot be satisfied inside one faction. If this is zero, the
    /// four planet-locked ores never need to meet and cross-faction demand is structurally
    /// impossible — which is exactly the failure ADR-0008 warns about, so the invariant that
    /// watches for it must be able to see this go to zero on its own rather than by configuration.
    /// </remarks>
    public int Framewrights { get; init; } = 6;

    public int Traders { get; init; } = 5;

    /// <summary>
    /// Bots that buy hull sections and lose them, and make nothing.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The economy's <em>terminal demand</em>, and the thing whose absence broke the first run.
    /// Miners, refiners and crafters all sell to each other, but without someone who buys the
    /// finished good and destroys it, the last stage in the chain has no customer — every seller
    /// undercuts toward the price floor, revenue goes to nearly nothing while industry fees stay
    /// real, and the producers go broke. That is a missing consumer, not a missing sink.
    /// </para>
    /// <para>
    /// This is what players are. A player buys a ship because they intend to fly it and expect to
    /// lose it, and that expectation is the whole revenue base every upstream profession rests on.
    /// </para>
    /// </remarks>
    public int Pilots { get; init; } = 25;

    /// <summary>
    /// Chance per pilot per day of losing a hull section they own.
    /// </summary>
    /// <remarks>
    /// 5% is roughly one loss every three weeks of daily play — frequent enough to sustain demand,
    /// rare enough that losing a ship still reads as an event rather than as a running cost.
    /// </remarks>
    public double PilotLossChance { get; init; } = 0.05;

    /// <summary>
    /// Seconds each bot spends actively working per day.
    /// </summary>
    /// <remarks>
    /// Two hours. Gathering is rate-limited by wall clock, so this is the single biggest driver of
    /// raw material supply — and therefore of every downstream price.
    /// </remarks>
    public int ActiveSecondsPerDay { get; init; } = 7_200;

    /// <summary>Ore a single deposit holds when full.</summary>
    public int NodeCapacity { get; init; } = 200;

    /// <summary>Seconds until a depleted deposit refills.</summary>
    public int NodeRespawnSeconds { get; init; } = 1_200;

    /// <summary>How many deposits the population shares.</summary>
    public int NodeCount { get; init; } = 30;

    /// <summary>Credits each character receives once, from the onboarding chain.</summary>
    public Credits BootstrapCredits { get; init; } = Credits.FromWholeCredits(13_000);

    /// <summary>
    /// Credits per character per day from repeatable sidequests — the steady-state faucet.
    /// </summary>
    /// <remarks>
    /// Zero by default, matching the game as it stands: repeatable sidequests are designed but not
    /// yet written. Raising this is how the equilibrium <c>F ≈ S</c> gets found.
    /// </remarks>
    public Credits DailyQuestCredits { get; init; } = Credits.Zero;

    /// <summary>
    /// Fraction of manufactured goods destroyed per day.
    /// </summary>
    /// <remarks>
    /// Stands in for combat losses, which do not exist yet. Zero by default so the simulation
    /// reports the game as it actually is rather than as it is intended to become.
    /// </remarks>
    public double DailyLossRate { get; init; }

    /// <summary>
    /// Extra broker fee charged at the capital, in basis points on top of the normal fee.
    /// </summary>
    /// <remarks>
    /// The capital is the only venue carrying all four planet-locked ores, so it is strictly more
    /// useful than any homeworld. Something has to pay for that, or every seller lists there and
    /// the four local books die — the failure mode this task exists to tune against.
    /// </remarks>
    public int CapitalFeePremiumBasisPoints { get; init; } = 5_000;

    /// <summary>
    /// Days between a bot's trips to the capital.
    /// </summary>
    /// <remarks>
    /// Flight time, modelled as trading frequency rather than as a position. What matters
    /// economically is not where a ship is but how often somebody can act on the distant book, and
    /// a bot that can only reach the capital every few days leaves its local market worth using.
    /// </remarks>
    public int CapitalTripDays { get; init; } = 3;

    /// <summary>
    /// Whether pilots buy and lose alloy frames as well as hull sections.
    /// </summary>
    /// <remarks>
    /// <para>
    /// False by default, because that is the game as authored: <c>alloy_frame</c> is produced by
    /// <c>build_alloy_frame</c> and consumed by no recipe at all. A good nobody consumes has no
    /// steady demand, so framewrights build a handful, cannot sell them, and stop — and the
    /// cross-faction trade that only they create stops with them.
    /// </para>
    /// <para>
    /// Setting this true stands in for the content that would fix it: some higher-tier hull whose
    /// recipe takes a frame. It exists so the cross-faction invariant can be shown to go green
    /// when the cause is removed, rather than being a check that is simply always red.
    /// </para>
    /// </remarks>
    public bool PilotsFlyFrameHulls { get; init; }

    /// <summary>Seed for bot decisions, so a run is exactly reproducible.</summary>
    public ulong Seed { get; init; } = 20_260_730;

    /// <summary>Where to write the per-day CSV, or null to skip it.</summary>
    public string? CsvPath { get; init; }

    public int TotalBots => Miners + Refiners + Crafters + Framewrights + Traders + Pilots;
}
