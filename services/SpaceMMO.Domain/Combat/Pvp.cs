using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Domain.Combat;

/// <summary>
/// Everything one attack needs to be judged, gathered by the caller.
/// </summary>
/// <remarks>
/// A record of facts rather than a pile of arguments, so adding a rule later does not
/// re-order a parameter list that several callers pass positionally. Nothing here is looked
/// up: the caller reads the two characters and the clock, and <see cref="Pvp.CanAttack"/>
/// decides. That is what keeps the rule testable with no database and identical on the server
/// and in a simulation.
/// </remarks>
public readonly record struct AttackAttempt
{
    /// <summary>The faction of whoever is pulling the trigger.</summary>
    public required Faction AttackerFaction { get; init; }

    /// <summary>The faction of whoever is being shot at.</summary>
    public required Faction TargetFaction { get; init; }

    /// <summary>Where this is happening. Both parties are assumed to be in weapons range.</summary>
    public required SystemPosition Position { get; init; }

    /// <summary>
    /// Whether the target is still under new-player protection — that is, has not finished the
    /// <c>main_story</c> chain (ADR-0008).
    /// </summary>
    public bool TargetIsProtected { get; init; }

    /// <summary>
    /// When the target last attacked <em>this</em> attacker, if they have.
    /// </summary>
    /// <remarks>
    /// Pairwise on purpose. Shooting somebody makes you answerable to them and to nobody else,
    /// so a defender who fires on an intruder does not become fair game for every other ship in
    /// the system.
    /// </remarks>
    public DateTimeOffset? TargetAttackedAttackerAt { get; init; }

    /// <summary>The server's clock. Never the client's.</summary>
    public required DateTimeOffset Now { get; init; }
}

/// <summary>
/// Whether one character may shoot another, per ADR-0008.
/// </summary>
/// <remarks>
/// <para>
/// <strong>A pure function, deliberately.</strong> There is no regions table, no zone actor,
/// and no PvP flag on a character: legality is recomputed from a position and two factions
/// every time it is asked. Nothing can drift out of sync with the world because nothing is
/// stored, and the same answer comes back on the server, in a test, and in EconSim.
/// </para>
/// <para>
/// The shape of it is three regions and one exception. The anchorage is safe. The contested
/// approach is hot for both factions, so the only route to the global market carries risk.
/// Beyond that, space belongs to somebody: the locals may engage an intruder, and the intruder
/// may only answer back — which is what makes a raid a raid rather than a hunt.
/// </para>
/// </remarks>
public static class Pvp
{
    /// <summary>Whether the attack is legal.</summary>
    public static bool CanAttack(FactionSpace space, in AttackAttempt attempt)
    {
        ArgumentNullException.ThrowIfNull(space);

        // Friendly fire is never legal, anywhere, under any provocation. Faction is derived
        // from race and cannot be changed, so this is not a state anyone can talk their way
        // out of.
        if (attempt.AttackerFaction == attempt.TargetFaction)
        {
            return false;
        }

        PvpZone zone = space.ZoneAt(attempt.Position);

        // The anchorage is absolute, and it outranks retaliation. Somebody who is shot in the
        // contested ring and chases their attacker to the capital does not get to finish it
        // over the docks — otherwise the one safe place in the system is only safe until
        // somebody arrives angry.
        if (zone == PvpZone.Anchorage)
        {
            return false;
        }

        bool provoked = WasProvoked(space, attempt);

        // New players are safe until the story that ends at the capital is finished, because
        // their last tutorial leg crosses the contested ring in the ship they just built.
        //
        // Provocation still overrides it. Protection exists so that nobody loses that ship to
        // a gank, not so that it can be used as a shield to shoot from — a protected player
        // who opens fire can be shot back at by the person they hit.
        if (attempt.TargetIsProtected && !provoked)
        {
            return false;
        }

        // The contested approach is hot on both sides of the divide. Nobody has to be
        // provoked and nobody is a local: this ring is the risk attached to the only global
        // market, and it is the reason the faction line touches the economy at all.
        if (zone == PvpZone.Approach)
        {
            return true;
        }

        // Out in faction space, whose half it is decides who may start something. The locals
        // may engage an intruder on sight; the intruder may not fire first, and may answer
        // whoever fired at them.
        return space.TerritoryAt(attempt.Position) == attempt.AttackerFaction || provoked;
    }

    /// <summary>
    /// Whether the target has already attacked this attacker recently enough to answer.
    /// </summary>
    /// <remarks>
    /// Exposed because "am I allowed to shoot back, and for how much longer" is a question the
    /// client will want to answer without asking the server, and it must be the same rule.
    /// </remarks>
    public static bool WasProvoked(FactionSpace space, in AttackAttempt attempt)
    {
        ArgumentNullException.ThrowIfNull(space);

        if (attempt.TargetAttackedAttackerAt is not { } attackedAt)
        {
            return false;
        }

        TimeSpan elapsed = attempt.Now - attackedAt;

        // A negative elapsed time means the recorded attack is in the future, which is a clock
        // problem rather than a provocation. Refusing it keeps a bad timestamp from opening a
        // permanent licence to shoot somebody.
        return elapsed >= TimeSpan.Zero && elapsed <= space.RetaliationWindow;
    }
}
