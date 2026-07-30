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
