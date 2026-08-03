using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Domain.Content;

/// <summary>A skill, as authored in <c>data/skills/</c>.</summary>
public sealed record SkillContent(string Key, string Name, SkillCategory Category);

/// <summary>An item definition, as authored in <c>data/items/</c>.</summary>
public sealed record ItemContent(string Key, string Name, ItemCategory Category, double VolumeM3);

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
public sealed record ResourceNodeContent(
    string Key,
    string Body,
    string Item,
    string Skill,
    int RequiredLevel,
    int QuantityMax,
    int RespawnSeconds,
    double[] Direction);

/// <summary>A station, as authored in <c>data/universe/</c>.</summary>
public sealed record StationContent(
    string Key,
    string Name,
    string System,
    string? Body,
    StationKind Kind);

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
