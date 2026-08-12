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
    double RadiusKm);

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
