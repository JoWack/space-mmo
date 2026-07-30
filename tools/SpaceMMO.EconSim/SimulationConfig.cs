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

    public int Traders { get; init; } = 5;

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

    /// <summary>Seed for bot decisions, so a run is exactly reproducible.</summary>
    public ulong Seed { get; init; } = 20_260_730;

    /// <summary>Where to write the per-day CSV, or null to skip it.</summary>
    public string? CsvPath { get; init; }

    public int TotalBots => Miners + Refiners + Crafters + Traders;
}
