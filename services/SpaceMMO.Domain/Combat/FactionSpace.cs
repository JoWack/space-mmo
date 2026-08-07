using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Domain.Combat;

/// <summary>
/// Which of the three regions of the system a point falls in (ADR-0008).
/// </summary>
public enum PvpZone
{
    /// <summary>Within <c>R_safe</c> of the capital. No PvP at all.</summary>
    Anchorage = 0,

    /// <summary>
    /// The contested ring around the capital. Hot on both sides of the divide: opposing
    /// factions may attack each other unprovoked.
    /// </summary>
    Approach = 1,

    /// <summary>
    /// Everything further out, which belongs to one faction or the other. The locals may
    /// engage intruders; intruders may only answer back.
    /// </summary>
    FactionSpace = 2,
}

/// <summary>
/// The shape of the system's PvP rules, as content rather than code.
/// </summary>
/// <remarks>
/// <para>
/// The capital sits at the system origin with the four homeworlds around it (ADR-0007), and a
/// plane through that origin splits the rest in two. Everything here is a tunable number: if
/// the contested ring turns out to be campable, widening <see cref="SafeRadiusKilometres"/>
/// shrinks the exposure without changing a single rule.
/// </para>
/// </remarks>
public sealed record FactionSpace
{
    /// <summary>Where the capital is. The centre of both radii and a point on the divide.</summary>
    public SystemPosition Capital { get; init; } = SystemPosition.Origin;

    /// <summary>No PvP within this distance of the capital.</summary>
    public double SafeRadiusKilometres { get; init; } = 500.0;

    /// <summary>Contested out to this distance, then faction space beyond it.</summary>
    public double ContestedRadiusKilometres { get; init; } = 2_000.0;

    /// <summary>
    /// The direction <see cref="Faction.A"/>'s half lies in, as a unit vector from the capital.
    /// </summary>
    /// <remarks>
    /// Faction B's half is the other side of the plane through the capital perpendicular to
    /// this. A point exactly on the plane is treated as Faction A's, because a boundary has to
    /// belong to somebody and a coin toss on a knife edge would make the rule flicker for
    /// anyone sitting on it.
    /// </remarks>
    public (double X, double Y, double Z) FactionADirection { get; init; } = (1.0, 0.0, 0.0);

    /// <summary>
    /// How long after being attacked a victim may answer back.
    /// </summary>
    /// <remarks>
    /// Long enough to turn around and fight, short enough that a single exchange does not leave
    /// somebody attackable for the rest of the evening.
    /// </remarks>
    public TimeSpan RetaliationWindow { get; init; } = TimeSpan.FromMinutes(5);

    /// <summary>Which region a point falls in.</summary>
    public PvpZone ZoneAt(SystemPosition position)
    {
        double distance = position.DistanceTo(Capital);

        if (distance <= SafeRadiusKilometres)
        {
            return PvpZone.Anchorage;
        }

        return distance <= ContestedRadiusKilometres ? PvpZone.Approach : PvpZone.FactionSpace;
    }

    /// <summary>
    /// Whose half of the system a point is in, ignoring the radii.
    /// </summary>
    /// <remarks>
    /// Every point belongs to one faction or the other — the plane partitions the whole system,
    /// including the capital's own regions. Those regions override the ownership rather than
    /// escaping it, which is what makes the contested ring hot on both sides.
    /// </remarks>
    public Faction TerritoryAt(SystemPosition position)
    {
        var offset = new SystemPosition(
            position.XKilometres - Capital.XKilometres,
            position.YKilometres - Capital.YKilometres,
            position.ZKilometres - Capital.ZKilometres);

        double side = offset.Dot(FactionADirection.X, FactionADirection.Y, FactionADirection.Z);

        return side >= 0.0 ? Faction.A : Faction.B;
    }
}
