namespace SpaceMMO.Domain.Gathering;

/// <summary>
/// Whether a deposit's remaining quantity is shared between players or tracked per character.
/// </summary>
/// <remarks>
/// <para>
/// Stored <strong>per node</strong> rather than as one global setting. Both models use the same
/// depletion table — a shared node has a single state row owned by nobody, a per-character node
/// has one row per gatherer — so switching a node between them is a data change, never a
/// migration.
/// </para>
/// <para>
/// Per-node granularity is deliberate. If shared nodes turn out to make the starting planets
/// miserable for new players, those specific nodes can be switched to
/// <see cref="PerCharacter"/> without giving up contention in deep space, where competition over
/// good deposits is the point.
/// </para>
/// </remarks>
public enum NodeSharingModel
{
    /// <summary>
    /// One pool everyone draws from. Depleting it denies it to everyone until it respawns, which
    /// is what makes a rich deposit worth reaching first and worth defending.
    /// </summary>
    Shared = 0,

    /// <summary>
    /// Each character has their own quantity over the same deposit. Removes competition and
    /// therefore removes griefing — appropriate where new players gather, and nowhere that
    /// territory is meant to matter.
    /// </summary>
    PerCharacter = 1,
}
