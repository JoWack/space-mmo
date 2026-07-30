using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Data.Entities;

/// <summary>
/// One credit movement. Append-only, and authoritative over
/// <see cref="Character.Balance"/> (ADR-0005).
/// </summary>
/// <remarks>
/// <para>
/// No updates, no deletes, ever. A mistake is corrected with a compensating entry, never by
/// editing history — that is what makes a dupe bug auditable after the fact instead of
/// invisible and unfixable.
/// </para>
/// <para>
/// This table grows without bound and will be among the largest in the database. It needs
/// time partitioning eventually, and must be archived rather than pruned.
/// </para>
/// </remarks>
public class LedgerEntry
{
    public long Id { get; set; }

    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    /// <summary>Signed change. Negative for charges, positive for receipts.</summary>
    public Credits DeltaCredits { get; set; }

    /// <summary>
    /// Why. Required, because faucet and sink attribution is a <c>GROUP BY</c> over this
    /// column and an unclassified flow breaks the EconSim invariants.
    /// </summary>
    public LedgerReason Reason { get; set; }

    /// <summary>
    /// Identifier of the causing trade, job, quest, or policy. Not a foreign key — it points
    /// into different tables depending on <see cref="Reason"/>.
    /// </summary>
    public long? ReferenceId { get; set; }

    public DateTimeOffset CreatedAt { get; set; }
}

/// <summary>
/// How much capped-faucet credit a character has been granted on one UTC day.
/// </summary>
/// <remarks>
/// The persistence behind <c>FaucetBudget</c>. Keyed by UTC date rather than a rolling
/// window: rolling is marginally fairer across timezones and considerably more work to
/// implement, explain, and debug, and players learn a fixed reset quickly.
/// </remarks>
public class CharacterFaucetDaily
{
    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    /// <summary>The UTC day this budget covers.</summary>
    public DateOnly UtcDate { get; set; }

    /// <summary>Total granted so far today, across every capped faucet source.</summary>
    public Credits CreditsGranted { get; set; }
}

/// <summary>
/// An insurance policy on one hull instance.
/// </summary>
public class InsurancePolicy
{
    public long Id { get; set; }

    /// <summary>The insured hull. Policies cover hulls only, not fitted modules or cargo.</summary>
    public long ItemInstanceId { get; set; }

    public ItemInstance? ItemInstance { get; set; }

    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    public InsuranceTier Tier { get; set; }

    /// <summary>Premium actually charged. Non-refundable, so there is no free option.</summary>
    public Credits PremiumPaid { get; set; }

    /// <summary>
    /// The hull's acquisition value at purchase time, copied here so a payout is reproducible
    /// even after the instance row is gone.
    /// </summary>
    public Credits InsuredValue { get; set; }

    public DateTimeOffset PurchasedAt { get; set; }

    public DateTimeOffset ExpiresAt { get; set; }

    /// <summary>Set when paid out. A policy pays at most once.</summary>
    public DateTimeOffset? ClaimedAt { get; set; }

    /// <summary>What was actually paid. Null until claimed.</summary>
    public Credits? PayoutAmount { get; set; }
}

/// <summary>
/// A death and the seed its loot resolution used.
/// </summary>
/// <remarks>
/// Recording the seed is what makes a player dispute over a lost item replayable from the log
/// rather than a matter of opinion, and lets EconSim reproduce destruction runs exactly
/// (ADR-0006).
/// </remarks>
public class DeathRecord
{
    public long Id { get; set; }

    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    public Domain.Loss.DeathCause Cause { get; set; }

    /// <summary>
    /// The seed passed to <c>DeathResolver.Resolve</c>. Stored as int64 and reinterpreted as
    /// uint64, since Postgres has no unsigned types.
    /// </summary>
    public long ResolutionSeed { get; set; }

    public int StarSystemId { get; set; }

    public int? BodyId { get; set; }

    /// <summary>The character responsible, if any. Null for environmental deaths.</summary>
    public int? KillerCharacterId { get; set; }

    public DateTimeOffset OccurredAt { get; set; }
}
