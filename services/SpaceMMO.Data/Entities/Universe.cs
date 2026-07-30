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
/// A depletable resource deposit — the only place material enters the economy.
/// </summary>
/// <remarks>
/// A delta over generated state (ADR-0002): the generator says where deposits are, and these
/// rows record how much players have taken out.
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

    public int QuantityRemaining { get; set; }

    /// <summary>Full quantity restored on respawn.</summary>
    public int QuantityMax { get; set; }

    /// <summary>When the node refills. Null if it is not depleted.</summary>
    public DateTimeOffset? RespawnAt { get; set; }
}
