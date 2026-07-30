namespace SpaceMMO.Domain.Universe;

/// <summary>
/// Kinds of celestial body within a star system.
/// </summary>
/// <remarks>
/// Bodies are generated deterministically from the system seed (ADR-0002); rows exist only
/// for bodies that players have interacted with, or that are hand-authored like the four
/// starting planets and the capital world.
/// </remarks>
public enum BodyKind
{
    /// <summary>The system's primary. Not landable.</summary>
    Star = 0,

    /// <summary>A landable planet.</summary>
    Planet = 1,

    /// <summary>A landable moon orbiting a planet.</summary>
    Moon = 2,

    /// <summary>A mineable belt. Not landable.</summary>
    AsteroidBelt = 3,

    /// <summary>A gas giant. Not landable, but harvestable from orbit.</summary>
    GasGiant = 4,
}

/// <summary>
/// What a station is for, per design-bible §1.
/// </summary>
/// <remarks>
/// A single station may combine roles in practice; this is its primary designation, used for
/// UI grouping and for deciding which services it offers.
/// </remarks>
public enum StationKind
{
    /// <summary>A market hub with an order book.</summary>
    TradingHub = 0,

    /// <summary>Ship docking, refitting, and industry facilities.</summary>
    Spaceport = 1,

    /// <summary>Player housing.</summary>
    Housing = 2,

    /// <summary>Social space with no economic function.</summary>
    Social = 3,

    /// <summary>The capital world's main hub: everything, plus the career quest givers.</summary>
    Capital = 4,
}

/// <summary>
/// A region's lawfulness, which governs whether player-versus-player killing is punished.
/// </summary>
/// <remarks>
/// Stored on systems and bodies from the first migration even though the criminal-flagging
/// and bounty systems are M4 work (ADR-0006). It is one column now; it is a migration over
/// live data later.
/// </remarks>
public enum SecurityLevel
{
    /// <summary>Aggression is prevented outright. The starting system and capital world.</summary>
    Secure = 0,

    /// <summary>Aggression is permitted but draws a criminal flag and enables bounties.</summary>
    Low = 1,

    /// <summary>No rules and no bounty system. The high-risk frontier.</summary>
    Null = 2,
}
