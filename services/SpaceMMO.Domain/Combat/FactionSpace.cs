using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Content;
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
/// The capital sits among the four homeworlds (ADR-0007), and a plane through it splits the rest
/// in two. Everything here is a tunable number: if the contested ring turns out to be campable,
/// widening <see cref="SafeRadiusKilometres"/> shrinks the exposure without changing a single rule.
/// </para>
/// <para>
/// <strong>No defaults, since task 104.</strong> This used to default to a capital at the origin, a
/// 500 km anchorage, a 2,000 km ring and a divide along +X, which described no system that exists:
/// every authored body lies within 251 km of the origin, so the whole system was anchorage, and the +X
/// divide put Terra in Faction B's half. The real numbers are content (<c>factionSpace</c> in
/// <c>data/universe/origin.json</c>), checked against the planets by the validator; build one with
/// <see cref="FromContent"/>.
/// </para>
/// </remarks>
public sealed record FactionSpace
{
    /// <summary>Where the capital is. The centre of both radii and a point on the divide.</summary>
    public required SystemPosition Capital { get; init; }

    /// <summary>No PvP within this distance of the capital.</summary>
    public required double SafeRadiusKilometres { get; init; }

    /// <summary>Contested out to this distance, then faction space beyond it.</summary>
    public required double ContestedRadiusKilometres { get; init; }

    /// <summary>
    /// The direction <see cref="Faction.A"/>'s half lies in, as a unit vector from the capital.
    /// </summary>
    /// <remarks>
    /// Faction B's half is the other side of the plane through the capital perpendicular to
    /// this. A point exactly on the plane is treated as Faction A's, because a boundary has to
    /// belong to somebody and a coin toss on a knife edge would make the rule flicker for
    /// anyone sitting on it.
    /// </remarks>
    public required (double X, double Y, double Z) FactionADirection { get; init; }

    /// <summary>
    /// How long after being attacked a victim may answer back.
    /// </summary>
    /// <remarks>
    /// Long enough to turn around and fight, short enough that a single exchange does not leave
    /// somebody attackable for the rest of the evening.
    /// </remarks>
    public required TimeSpan RetaliationWindow { get; init; }

    /// <summary>The faction space content authors, centred on the Capital body's position.</summary>
    /// <param name="content">The authored numbers.</param>
    /// <param name="capital">Where the body <see cref="FactionSpaceContent.Capital"/> names stands.</param>
    public static FactionSpace FromContent(FactionSpaceContent content, SystemPosition capital)
    {
        ArgumentNullException.ThrowIfNull(content);

        return new FactionSpace
        {
            Capital = capital,
            SafeRadiusKilometres = content.SafeRadiusKm,
            ContestedRadiusKilometres = content.ContestedRadiusKm,
            FactionADirection =
                (content.FactionADirection[0], content.FactionADirection[1], content.FactionADirection[2]),
            RetaliationWindow = TimeSpan.FromSeconds(content.RetaliationSeconds),
        };
    }

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
