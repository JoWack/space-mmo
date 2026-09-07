using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Progression;

namespace SpaceMMO.Data.Entities;

/// <summary>A player account. One account may hold several characters.</summary>
public class Account
{
    public int Id { get; set; }

    public required string Email { get; set; }

    /// <summary>
    /// A password <em>hash</em>, never a password. Algorithm and parameters are the auth
    /// layer's concern; this column only ever stores the encoded result.
    /// </summary>
    public required string PasswordHash { get; set; }

    public DateTimeOffset CreatedAt { get; set; }

    public ICollection<Character> Characters { get; } = [];
}

/// <summary>A playable character.</summary>
public class Character
{
    public int Id { get; set; }

    public int AccountId { get; set; }

    public Account? Account { get; set; }

    public required string Name { get; set; }

    /// <summary>
    /// Chosen at creation and never changeable.
    /// </summary>
    /// <remarks>
    /// Faction is <em>not</em> stored: it is derived via <see cref="Races.FactionFor"/>, so a
    /// row cannot claim a Space Orc in Faction A. A constraint you cannot violate beats one
    /// you have to remember to check.
    /// </remarks>
    public Race Race { get; set; }

    /// <summary>The starting planet, set from <see cref="Races.HomeBodyKeyFor"/> at creation.</summary>
    public int HomeBodyId { get; set; }

    public Body? HomeBody { get; set; }

    /// <summary>
    /// Cached balance. The ledger is authoritative (ADR-0005) — when they disagree, this is
    /// what is wrong.
    /// </summary>
    public Credits Balance { get; set; }

    public DateTimeOffset CreatedAt { get; set; }

    /// <summary>
    /// When this character last extracted from a resource node. Null if they never have.
    /// </summary>
    /// <remarks>
    /// The server's rate limit on gathering. Entitlement is computed from elapsed wall-clock time
    /// since this moment, so a client asking a hundred times a second extracts exactly as much as
    /// one asking at the tick interval. A single column rather than one per node, because a
    /// character can only work one deposit at a time.
    /// </remarks>
    public DateTimeOffset? LastGatheredAt { get; set; }

    /// <summary>
    /// The station this character is docked at, or null when they are not.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>A place, not a permission.</strong> This says where the character is, and services
    /// ask whether that place is the one they need. It is deliberately not a "may use the market"
    /// flag: station interiors are coming, and when they do, "walk to the refinery" becomes a
    /// second check inside this station rather than a rewrite of every service that consults it.
    /// </para>
    /// <para>
    /// Set only by the game server, which is the only party that knows where a ship actually is.
    /// A client claiming to be docked is a client claiming a position, and positions are not
    /// something a client is trusted to report (ADR-0003).
    /// </para>
    /// </remarks>
    public int? DockedStationId { get; set; }

    /// <summary>
    /// The hull this character flies, or would summon. Null for somebody on foot with no ship.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>New state ADR-0012 named as a cost and nobody modelled.</strong> A character may own
    /// several hulls and exactly one is the one they are flying; the hold's reachability depends on
    /// which, and so does knowing what pawn to put them in.
    /// </para>
    /// <para>
    /// Where the ship <em>is</em> needs no column of its own: an owned hull is an
    /// <c>ItemInstance</c> sitting in an inventory, and for a parked ship that inventory is the
    /// station hangar it was left in. Summoning moves the instance to the hangar of the station the
    /// player is standing in, which is what "summoning elsewhere moves it" means in rows.
    /// </para>
    /// </remarks>
    public long? ActiveShipItemInstanceId { get; set; }

    public ItemInstance? ActiveShipItemInstance { get; set; }

    /// <summary>
    /// The hull this character is sitting in, or null for somebody on foot.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>The half of ADR-0012 point 4 the server could not prove.</strong> A hold is reachable
    /// "docked at a station with their active ship, <em>or sitting in that ship</em>", and nothing
    /// here knew whether anybody was aboard. Being undocked could not stand in for it: a character
    /// walking around a planet is undocked too, and that would open the hold from a rock.
    /// </para>
    /// <para>
    /// <strong>Not the same fact as <see cref="LastSeenFlying"/>.</strong> That says a player was in
    /// a ship pawn; this says which owned hull. They differ while the unowned prop ship exists,
    /// which is exactly the case that must not open somebody's hold from a shuttle they left at
    /// another station.
    /// </para>
    /// <para>
    /// Durable across sessions, deliberately. Task 147 restores a player flying the ship they quit
    /// in, so "sitting in hull 4" has to survive a disconnect — it is cleared by stepping out, not
    /// by logging off.
    /// </para>
    /// </remarks>
    public long? AboardShipItemInstanceId { get; set; }

    public ItemInstance? AboardShipItemInstance { get; set; }

    /// <summary>
    /// Where this character was last seen, in system kilometres, or null if they never have been.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>Three columns rather than a type, because null has to mean something.</strong> A
    /// character who has never been anywhere is not at the origin; they have no position at all, and
    /// that is the one case that gets sent to a starting point instead of restored. A struct of
    /// three doubles would have to invent a sentinel to say the same thing.
    /// </para>
    /// <para>
    /// Written by the game server and nobody else, for the reason
    /// <see cref="DockedStationId"/> is: this is a position, and a client that could report its own
    /// position could report any of them (ADR-0003).
    /// </para>
    /// </remarks>
    public double? LastSystemX { get; set; }

    public double? LastSystemY { get; set; }

    public double? LastSystemZ { get; set; }

    /// <summary>
    /// Whether they were flying when last seen, rather than standing on something.
    /// </summary>
    /// <remarks>
    /// <strong>Not derivable from <see cref="ActiveShipItemInstanceId"/>.</strong> That says which
    /// hull is theirs to fly, and it stays set while they are stood on a planet beside it. The
    /// question here is what they were doing, and the two answers differ every time somebody parks.
    /// </remarks>
    public bool LastSeenFlying { get; set; }

    public Station? DockedStation { get; set; }

    public ICollection<CharacterSkill> Skills { get; } = [];

    public ICollection<Inventory> Inventories { get; } = [];
}

/// <summary>A skill definition. Seeded from the design bible, not created at runtime.</summary>
public class Skill
{
    public int Id { get; set; }

    /// <summary>Stable key, e.g. <c>mining</c>.</summary>
    public required string Key { get; set; }

    public required string Name { get; set; }

    public SkillCategory Category { get; set; }
}

/// <summary>
/// One character's experience in one skill.
/// </summary>
/// <remarks>
/// Stores XP only. Level is always derived through <see cref="SkillCurve.LevelForXp"/>
/// (ADR-0004), which makes it impossible for level and XP to disagree.
/// </remarks>
public class CharacterSkill
{
    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    public int SkillId { get; set; }

    public Skill? Skill { get; set; }

    /// <summary>Total accumulated XP, including overflow past level 99.</summary>
    public long Xp { get; set; }
}
