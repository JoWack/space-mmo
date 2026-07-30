using SpaceMMO.Domain.Gathering;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Data.Entities;

/// <summary>
/// A star system. Only systems players have touched get rows — the rest are recomputed from
/// the galaxy seed on demand (ADR-0002).
/// </summary>
public class StarSystem
{
    public int Id { get; set; }

    /// <summary>Stable key, e.g. <c>system_origin</c>.</summary>
    public required string Key { get; set; }

    public required string Name { get; set; }

    /// <summary>Galaxy-space position in int64 units. Never enters the Unreal world (ADR-0001).</summary>
    public long GalaxyX { get; set; }

    public long GalaxyY { get; set; }

    public long GalaxyZ { get; set; }

    /// <summary>Seed this system's contents are generated from.</summary>
    public long Seed { get; set; }

    /// <summary>
    /// Which version of the generator produced this system.
    /// </summary>
    /// <remarks>
    /// Once players own property here, this system's generator is frozen — changing the
    /// algorithm would move the ground under their structures. Costs one column now and is
    /// unimplementable later (ADR-0002).
    /// </remarks>
    public int GeneratorVersion { get; set; }

    public SecurityLevel SecurityLevel { get; set; }

    public ICollection<Body> Bodies { get; } = [];

    public ICollection<Station> Stations { get; } = [];
}

/// <summary>A planet, moon, star, or belt within a system.</summary>
public class Body
{
    public int Id { get; set; }

    /// <summary>Stable key, e.g. <c>body_terra</c>. Referenced by <c>Races.HomeBodyKeyFor</c>.</summary>
    public required string Key { get; set; }

    public required string Name { get; set; }

    public int StarSystemId { get; set; }

    public StarSystem? StarSystem { get; set; }

    public BodyKind Kind { get; set; }

    /// <summary>
    /// May differ from the parent system's level — a secure system can contain a lawless moon.
    /// </summary>
    public SecurityLevel SecurityLevel { get; set; }

    /// <summary>Radius in kilometres, at the 1:10 scale from ADR-0001.</summary>
    public double RadiusKm { get; set; }

    public ICollection<Station> Stations { get; } = [];
}

/// <summary>A station: trading hub, spaceport, housing, or the capital hub.</summary>
public class Station
{
    public int Id { get; set; }

    public required string Key { get; set; }

    public required string Name { get; set; }

    /// <summary>
    /// Denormalised from the parent body on purpose.
    /// </summary>
    /// <remarks>
    /// Carried on every station so that market and inventory queries can filter by system
    /// without joining through bodies, and so sharding by system stays a deployment change
    /// rather than a schema rewrite (ADR-0003).
    /// </remarks>
    public int StarSystemId { get; set; }

    public StarSystem? StarSystem { get; set; }

    /// <summary>Null for deep-space stations that do not orbit a body.</summary>
    public int? BodyId { get; set; }

    public Body? Body { get; set; }

    public StationKind Kind { get; set; }
}

/// <summary>
/// A resource deposit — the only place material enters the economy.
/// </summary>
/// <remarks>
/// <para>
/// The <em>definition</em>: where the deposit is, what it yields, and how much it holds when
/// full. How much is currently left lives in <see cref="ResourceNodeState"/>, because that
/// depends on who is asking.
/// </para>
/// <para>
/// A delta over generated state (ADR-0002): the generator says where deposits are, and these
/// rows exist for the ones players have touched.
/// </para>
/// </remarks>
public class ResourceNode
{
    public long Id { get; set; }

    public int StarSystemId { get; set; }

    public StarSystem? StarSystem { get; set; }

    public int BodyId { get; set; }

    public Body? Body { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    /// <summary>Full quantity, restored on respawn.</summary>
    public int QuantityMax { get; set; }

    /// <summary>Seconds from depletion until the node refills.</summary>
    public int RespawnSeconds { get; set; }

    /// <summary>
    /// The skill required to work this deposit, and the one that earns XP from it.
    /// </summary>
    public int SkillId { get; set; }

    public Skill? Skill { get; set; }

    /// <summary>Minimum level to gather here.</summary>
    public int RequiredLevel { get; set; }

    /// <summary>
    /// A tool that must be held, or null if the deposit can be worked by hand.
    /// </summary>
    /// <remarks>
    /// This is how the onboarding chain gates ore behind crafting a mining laser, while leaving
    /// surface scrap hand-gatherable so a brand-new character has somewhere to start.
    /// </remarks>
    public int? RequiredToolItemDefId { get; set; }

    public ItemDef? RequiredToolItemDef { get; set; }

    /// <summary>
    /// Whether depletion is shared between players or tracked per character.
    /// </summary>
    /// <remarks>
    /// Per node rather than global, so the model can be changed for the places it causes problems
    /// without giving it up everywhere. Both models use the same
    /// <see cref="ResourceNodeState"/> table, so switching one is an <c>UPDATE</c>, not a
    /// migration.
    /// </remarks>
    public NodeSharingModel SharingModel { get; set; }

    public ICollection<ResourceNodeState> States { get; } = [];
}

/// <summary>
/// How much of a deposit is left, and for whom.
/// </summary>
/// <remarks>
/// <para>
/// One table serving both sharing models, which is what makes them interchangeable:
/// </para>
/// <list type="bullet">
/// <item><see cref="NodeSharingModel.Shared"/> — a single row with a null
/// <see cref="CharacterId"/>, owned by nobody and drawn down by everyone.</item>
/// <item><see cref="NodeSharingModel.PerCharacter"/> — one row per gatherer, each with their own
/// quantity.</item>
/// </list>
/// <para>
/// Rows are created lazily on first extraction, so an untouched deposit costs nothing to store
/// no matter how many exist.
/// </para>
/// </remarks>
public class ResourceNodeState
{
    public long Id { get; set; }

    public long ResourceNodeId { get; set; }

    public ResourceNode? ResourceNode { get; set; }

    /// <summary>
    /// Null for a shared node's single pool; the owner for a per-character node.
    /// </summary>
    public int? CharacterId { get; set; }

    public Character? Character { get; set; }

    public int QuantityRemaining { get; set; }

    /// <summary>
    /// When the deposit refills. Null while it still holds material.
    /// </summary>
    /// <remarks>
    /// Respawn is applied lazily on the next access rather than by a background sweeper. A sweeper
    /// would have to touch every depleted node on a timer for no benefit — nothing observes a node
    /// except a player gathering from it.
    /// </remarks>
    public DateTimeOffset? RespawnAt { get; set; }
}
