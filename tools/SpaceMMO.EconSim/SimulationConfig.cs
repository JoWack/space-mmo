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

    /// <summary>One deposit type: how much it holds, how fast it refills, how many exist.</summary>
    public readonly record struct Deposit(int Capacity, int RespawnSeconds, int Count)
    {
        /// <summary>Units this deposit type can yield in a day, the hard ceiling on supply.</summary>
        public long DailyCapacity => (long)Capacity * Count * 86_400 / RespawnSeconds;
    }

    /// <summary>
    /// The deposits in <c>data/universe/origin.json</c>, per item.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Transcribed from the authored universe rather than assumed. The previous version carried a
    /// single capacity/respawn/count triple with a comment claiming it mirrored shipped content,
    /// and it did not: it assumed thirty deposits of everything, where the game authors two ferrite
    /// nodes and exactly one node for each planet-locked ore. That overstated raw supply fifteenfold
    /// for ferrite and sixtyfold for the ores that matter, and the resulting glut — 232 million
    /// units created against five million consumed, price pinned at the floor — was largely the
    /// simulator's own invention rather than a finding about the game.
    /// </para>
    /// <para>
    /// This is the "feed in the real value at least once" rule in CLAUDE.md: every number here has
    /// a counterpart in a JSON file, and if the two drift the simulation quietly answers a question
    /// about a game nobody is building.
    /// </para>
    /// </remarks>
    public Dictionary<string, Deposit> Deposits { get; init; } = new(StringComparer.Ordinal)
    {
        // Two nodes on the capital, 200 each, twenty-minute respawn.
        ["ferrite_ore"] = new Deposit(200, 1_200, 2),

        // One node per homeworld, 150 each, thirty-minute respawn. This is the real scarcity: an
        // entire planet's export comes out of a single deposit.
        ["terran_ferrite"] = new Deposit(150, 1_800, 1),
        ["ares_regolith"] = new Deposit(150, 1_800, 1),
        ["verdant_amber"] = new Deposit(150, 1_800, 1),
        ["grimhold_slag"] = new Deposit(150, 1_800, 1),
    };

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
    /// <para>
    /// Set from `--sweep capital`, and the sweep contradicted why this existed. The premium was
    /// added on the assumption that without it every seller would list at the capital and the four
    /// local books would die. Measured across 0 to 25,000 basis points, it barely moves anything:
    /// frames built go 111 to 90, and local trade counts stay inside their own noise. What actually
    /// keeps a homeworld's book alive is that plates and hull sections are made and consumed there,
    /// not that the capital is expensive.
    /// </para>
    /// <para>
    /// Kept at a modest 2,500 rather than removed, because it is still a credit sink and distance
    /// ought to cost something. It is no longer load-bearing, so it should not be treated as the
    /// lever if local books ever go quiet — look at whether anything is still manufactured locally.
    /// </para>
    /// </remarks>
    public int CapitalFeePremiumBasisPoints { get; init; } = 2_500;

    /// <summary>
    /// Days between a bot's trips to the capital.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Flight time, modelled as trading frequency rather than as a position. What matters
    /// economically is not where a ship is but how often somebody can act on the distant book, and
    /// a bot that can only reach the capital every few days leaves its local market worth using.
    /// </para>
    /// <para>
    /// Three, from `--sweep capital`: seven produces about a third fewer frames and a third less
    /// cross-faction ore, because ore spends its time in a hold rather than on a book. One is
    /// degenerate rather than better — a miner works the capital's ferrite when it is there and its
    /// homeworld's ore when it is not, so a one-day trip means it is never home and no
    /// planet-locked ore is mined at all. That row of the sweep reads as a dead economy and is an
    /// artefact of the model, not a finding about flight times.
    /// </para>
    /// </remarks>
    public int CapitalTripDays { get; init; } = 3;

    /// <summary>
    /// Whether pilots buy and lose the frame-consuming hull tier.
    /// </summary>
    /// <remarks>
    /// <para>
    /// True by default, because <c>assemble_hull_freighter</c> now exists: it takes two composite
    /// frames, so frames finally have a consumer. Before that recipe was authored the frame was a
    /// dead end, framewrights built about twenty across five simulated years and stopped, and the
    /// only reason ore ever crosses the faction line went with them.
    /// </para>
    /// <para>
    /// Modelled as pilots buying frames at the capital rather than as a separate shipwright buying
    /// frames, sections and thrusters to assemble a freighter. That abstraction is deliberate: what
    /// this simulation is measuring is whether frames have <em>standing demand</em>, and adding a
    /// third intermediary between the frame and the player who wants the ship changes the number of
    /// hops without changing the answer. Set false to reproduce the pre-freighter economy and watch
    /// the cross-faction invariant fail.
    /// </para>
    /// </remarks>
    public bool PilotsFlyFrameHulls { get; init; } = true;

    /// <summary>Seed for bot decisions, so a run is exactly reproducible.</summary>
    public ulong Seed { get; init; } = 20_260_730;

    /// <summary>Where to write the per-day CSV, or null to skip it.</summary>
    public string? CsvPath { get; init; }

    public int TotalBots => Miners + Refiners + Crafters + Framewrights + Traders + Pilots;
}
