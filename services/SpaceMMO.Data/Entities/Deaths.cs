using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Loss;

namespace SpaceMMO.Data.Entities;

/// <summary>
/// A death: who, how, by whom and when, and whether what it costs has been taken yet (ADR-0016, task 103).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Written with the kill, costed after it.</strong> The killing hit writes this under the pair's lock, which
/// every hit queued behind it waits out (task 198), so it does nothing else there. What the death costs is taken
/// afterwards by <c>DeathCostService</c>, once the game server says where the body fell, and
/// <see cref="CostsAppliedAt"/> says it has been, so it is taken once.
/// </para>
/// <para>
/// ADR-0006's resolution seed is gone with its resolver: ADR-0016 has no roll in it.
/// </para>
/// </remarks>
public class DeathRecord
{
    public long Id { get; set; }

    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    public DeathCause Cause { get; set; }

    public int StarSystemId { get; set; }

    public int? BodyId { get; set; }

    /// <summary>The character responsible, if any. Null for environmental deaths.</summary>
    public int? KillerCharacterId { get; set; }

    public DateTimeOffset OccurredAt { get; set; }

    /// <summary>When the materials were taken and the gear worn, or null while that is still owed.</summary>
    public DateTimeOffset? CostsAppliedAt { get; set; }
}

/// <summary>
/// What one death took of one material: how much was dropped where the body fell and how much was destroyed.
/// </summary>
/// <remarks>
/// The record of the sink, kept for EconSim and for a player asking where their ore went: ADR-0016's destroyed half
/// is the only material sink combat has, and it has to be measured again before its tuning means anything.
/// </remarks>
public class DeathLoss
{
    public long DeathRecordId { get; set; }

    public DeathRecord? DeathRecord { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    /// <summary>The stack's style, as it was carried.</summary>
    public ItemStyle Style { get; set; }

    public int Dropped { get; set; }

    public int Destroyed { get; set; }
}

/// <summary>
/// The dropped half of a death's materials, lying where the body fell for anyone to take (ADR-0016).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Not an inventory.</strong> It belongs to nobody, every character may take from it, and it is gone after ten
/// minutes; an <see cref="Inventory"/> is somebody's, and every route that lists or moves one would have to learn to
/// leave this alone. So it has its own rows and one way out, <c>DeathCostService.LootAsync</c>.
/// </para>
/// <para>
/// <strong>Gone means past <see cref="ExpiresAt"/></strong>, not deleted: what is still in it then was destroyed,
/// and the rows say how much. Nothing sweeps it.
/// </para>
/// </remarks>
public class LootContainer
{
    public long Id { get; set; }

    public long DeathRecordId { get; set; }

    public DeathRecord? DeathRecord { get; set; }

    /// <summary>Whose goods these were: the name over the crate, and who keeps the cost basis on taking them back.</summary>
    public int DroppedByCharacterId { get; set; }

    public Character? DroppedByCharacter { get; set; }

    public int StarSystemId { get; set; }

    /// <summary>Where it lies, in system kilometres: beside where the body fell, as the game server placed it.</summary>
    public double SystemX { get; set; }

    public double SystemY { get; set; }

    public double SystemZ { get; set; }

    public DateTimeOffset CreatedAt { get; set; }

    public DateTimeOffset ExpiresAt { get; set; }

    public ICollection<LootContainerItem> Items { get; } = [];
}

/// <summary>One material in a container. Deleted when the last of it is taken.</summary>
public class LootContainerItem
{
    public long LootContainerId { get; set; }

    public LootContainer? LootContainer { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    public ItemStyle Style { get; set; }

    public int Quantity { get; set; }

    /// <summary>The share of the dead character's cost basis that fell with these units.</summary>
    public Credits CostBasis { get; set; }
}
