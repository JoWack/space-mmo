using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Domain.Content;

/// <summary>A skill, as authored in <c>data/skills/</c>.</summary>
public sealed record SkillContent(string Key, string Name, SkillCategory Category);

/// <summary>An item definition, as authored in <c>data/items/</c>.</summary>
/// <param name="FactionBuyPrice">
/// Whole credits a faction standing order pays per unit, or null if no faction buys this.
/// Whole credits, not minor units, matching <see cref="QuestContent.RewardCredits"/> — authored
/// content speaks in the units a designer thinks in.
/// </param>
/// <remarks>
/// <para>
/// <strong>The faction price is a floor, not a valuation.</strong> It exists so a player holding
/// nothing but ore can always turn some of it into credits — at zero balance they can neither start
/// a job nor place a sell order, both of which charge up front. It must therefore be low enough that
/// selling to the faction is always the worst available option, or players will sell to it instead
/// of to each other and the player market never forms.
/// </para>
/// <para>
/// Priced on raw materials only. A standing bid on manufactured goods would put a floor under the
/// things players are supposed to compete on, which is the opposite of the point.
/// </para>
/// </remarks>
public sealed record ItemContent(
    string Key,
    string Name,
    ItemCategory Category,
    double VolumeM3,
    long? FactionBuyPrice = null,
    bool PlanetLocked = false)
{
    /// <summary>
    /// Whether this material occurs on exactly one body and nowhere else (ADR-0008).
    /// </summary>
    /// <remarks>
    /// Declared rather than inferred. "Every raw material appears on one planet" would be the
    /// wrong rule — the starter chain is deliberately everywhere, so that a new player is never
    /// waiting on a market that may have no sellers — and a rule that forbids the common case
    /// would be turned off rather than obeyed.
    ///
    /// Saying it out loud is also what lets the validator catch the failure that matters:
    /// planet-locked quietly becoming "mostly on one planet", which dissolves hauling as a
    /// profession without anything looking broken.
    /// </remarks>
    public bool PlanetLocked { get; init; } = PlanetLocked;
}

/// <summary>One material a recipe consumes.</summary>
public sealed record RecipeInputContent(string Item, int Quantity);

/// <summary>A recipe, as authored in <c>data/recipes/</c>.</summary>
public sealed record RecipeContent(
    string Key,
    string Output,
    int OutputQuantity,
    string Skill,
    int RequiredLevel,
    int JobSeconds,
    long XpPerRun,
    string? RequiredTool,
    IReadOnlyList<RecipeInputContent> Inputs);

/// <summary>One objective within a quest.</summary>
public sealed record QuestStepContent(
    int Ordinal,
    string Description,
    ObjectiveType Objective,
    string Target,
    int Quantity);

/// <summary>A quest, as authored in <c>data/quests/</c>.</summary>
/// <param name="RewardCredits">Whole credits, not minor units — content is authored in the unit
/// designers think in, and converted on load.</param>
public sealed record QuestContent(
    string Key,
    string Name,
    QuestKind Kind,
    string? Prerequisite,
    long RewardCredits,
    string? RewardSkill,
    long RewardXp,
    int? CooldownSeconds,
    IReadOnlyList<QuestStepContent> Steps);

/// <summary>A star system, as authored in <c>data/universe/</c>.</summary>
/// <param name="GalaxyX">Galaxy-space coordinates in kilometres, int64 (ADR-0001). These never
/// enter Unreal — they position systems relative to each other and nothing more.</param>
public sealed record StarSystemContent(
    string Key,
    string Name,
    long GalaxyX,
    long GalaxyY,
    long GalaxyZ,
    long Seed,
    SecurityLevel SecurityLevel);

/// <summary>A planet or moon, as authored in <c>data/universe/</c>.</summary>
/// <param name="RadiusKm">Already at the 1:10 universe scale (ADR-0001). The scale is applied
/// once, here in authored content, rather than at every conversion.</param>
public sealed record BodyContent(
    string Key,
    string Name,
    string System,
    BodyKind Kind,
    SecurityLevel SecurityLevel,
    double RadiusKm,
    BodyAppearanceContent? Appearance = null,
    BodyTerrainContent? Terrain = null);

/// <summary>
/// The shape of a body's ground, as authored in <c>data/universe/</c>.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Shape, not colour.</strong> The palette says what a world is made of; this says what it
/// looks like from a distance — whether it is smooth swells, rugged hills, or something jagged. Two
/// planets sharing a palette and differing here read as genuinely different places, and two sharing
/// these and differing in palette read as the same place painted twice.
/// </para>
/// <para>
/// These were constants in the client until 19 August, which meant every world had one silhouette
/// and the only thing content could change was its tint.
/// </para>
/// </remarks>
/// <param name="Seed">Decorrelates one body's surface from another's. Two bodies sharing a seed and
/// a frequency are the same landscape.</param>
/// <param name="MaxElevationKm">How far the surface rises above the nominal radius. Modest by
/// design: Everest is 0.14% of Earth's radius, and terrain built the other way round makes a planet
/// look like a golf ball.</param>
/// <param name="BaseFrequency">Features per radius at the coarsest octave. Low values give broad
/// swells; it was 2 and produced a planet whose steepest ground anywhere was 5.9 degrees, which no
/// slope-based material could say anything about.</param>
public sealed record BodyTerrainContent(
    long Seed,
    double MaxElevationKm,
    double BaseFrequency);

/// <summary>
/// What a body's ground looks like, as authored in <c>data/universe/</c>.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Here rather than in the client, because a planet's look is content.</strong> Ares is red
/// oxide and Grimhold is black slag for the same reason one is 339 km and the other 780 — somebody
/// decided, and wrote it down. Keeping the palette in C++ would mean the only way to say what a new
/// world looks like is to recompile the game, and would let a body's appearance and its facts drift
/// apart in a way nothing could catch.
/// </para>
/// <para>
/// The three colours are the ground low down, the ground high up, and the rock that shows on
/// anything steep. The two ranges remap the terrain's own measurements onto that blend: height and
/// steepness each occupy only part of 0..1 on any given planet, and a material fed the raw values
/// draws a flat colour however it is authored.
/// </para>
/// <para>
/// Optional, and null means the client keeps whatever material it was configured with. A body
/// nobody has painted yet is a working state, not a broken one.
/// </para>
/// </remarks>
/// <param name="LowColour">Ground at the bottom of the body's relief, as "r,g,b" in 0..1.</param>
/// <param name="HighColour">Ground at the top of it.</param>
/// <param name="RockColour">What shows through on slopes.</param>
/// <param name="HeightFrom">Where the height blend starts, in fractions of maximum relief.</param>
/// <param name="HeightTo">And where it finishes.</param>
/// <param name="SlopeFrom">Where rock starts showing, as the sine of the slope angle.</param>
/// <param name="SlopeTo">And where it covers the ground entirely.</param>
public sealed record BodyAppearanceContent(
    string LowColour,
    string HighColour,
    string RockColour,
    double HeightFrom,
    double HeightTo,
    double SlopeFrom,
    double SlopeTo);

/// <summary>A resource deposit, as authored in <c>data/universe/</c>.</summary>
/// <param name="Direction">Direction from the body's centre. Normalised on load, so authors may
/// write whole numbers rather than unit vectors.</param>
/// <param name="RequiredTool">Item key of a tool the character must hold, or null for bare hands.
/// The database column and <c>GathingService.GuardToolAsync</c> have always existed; this field did
/// not, so no authored deposit could ever require a tool and the gate was unreachable.</param>
public sealed record ResourceNodeContent(
    string Key,
    string Body,
    string Item,
    string Skill,
    int RequiredLevel,
    int QuantityMax,
    int RespawnSeconds,
    double[] Direction,
    string? RequiredTool = null);

/// <summary>A station, as authored in <c>data/universe/</c>.</summary>
public sealed record StationContent(
    string Key,
    string Name,
    string System,
    string? Body,
    StationKind Kind,
    double[]? Direction = null,
    double[]? SystemPosition = null,
    double DockingRangeKm = 5.0)
{
    /// <summary>
    /// Where it stands on its body, as a direction from that body's centre.
    /// </summary>
    /// <remarks>
    /// The same way deposits are placed, and for the same reasons: a latitude-longitude pair has
    /// two singular points and an outpost at a pole is no less valid than one on the equator, and
    /// storing an altitude would be a second answer to a question the terrain function already
    /// answers.
    /// </remarks>
    public double[]? Direction { get; init; } = Direction;

    /// <summary>Where it floats, for a station that orbits nothing. Kilometres.</summary>
    public double[]? SystemPosition { get; init; } = SystemPosition;

    /// <summary>How close a ship must be to dock, in kilometres.</summary>
    public double DockingRangeKm { get; init; } = DockingRangeKm;
}

/// <summary>
/// Everything loaded from <c>data/</c>, before it reaches the database.
/// </summary>
/// <remarks>
/// Plain records with no persistence concerns, so <see cref="ContentValidator"/> can check a pack
/// without a database — which is what lets content be validated in a unit test and in CI rather
/// than only at server startup.
/// </remarks>
public sealed record ContentPack(
    IReadOnlyList<SkillContent> Skills,
    IReadOnlyList<ItemContent> Items,
    IReadOnlyList<RecipeContent> Recipes,
    IReadOnlyList<QuestContent> Quests,
    IReadOnlyList<StarSystemContent> Systems,
    IReadOnlyList<BodyContent> Bodies,
    IReadOnlyList<StationContent> Stations,
    IReadOnlyList<ResourceNodeContent> ResourceNodes)
{
    /// <summary>An empty pack, for tests and for merging.</summary>
    public static ContentPack Empty { get; } = new([], [], [], [], [], [], [], []);

    /// <summary>Combines two packs, so content can be split across many files.</summary>
    public ContentPack Concat(ContentPack other)
    {
        ArgumentNullException.ThrowIfNull(other);

        return new ContentPack(
            [.. Skills, .. other.Skills],
            [.. Items, .. other.Items],
            [.. Recipes, .. other.Recipes],
            [.. Quests, .. other.Quests],
            [.. Systems, .. other.Systems],
            [.. Bodies, .. other.Bodies],
            [.. Stations, .. other.Stations],
            [.. ResourceNodes, .. other.ResourceNodes]);
    }
}
