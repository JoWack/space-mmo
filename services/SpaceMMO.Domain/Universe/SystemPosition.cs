namespace SpaceMMO.Domain.Universe;

/// <summary>
/// A point in system space, in kilometres from the system's origin.
/// </summary>
/// <remarks>
/// <para>
/// The middle tier of the three-tier coordinate model (ADR-0001): <c>double</c> kilometres,
/// authoritative, and the frame everything in one system is expressed in. The client mirrors
/// this as <c>FSystemCoordinate</c>; the galaxy tier is <c>long</c> and does not appear here
/// because there is one system (ADR-0007).
/// </para>
/// <para>
/// A value type with no behaviour beyond distance, because the rules that use it — where PvP
/// is legal, whose territory a point is in — are pure functions over it and belong beside the
/// rest of the rules rather than inside the coordinate.
/// </para>
/// </remarks>
public readonly record struct SystemPosition(double XKilometres, double YKilometres, double ZKilometres)
{
    /// <summary>The system's origin, which is also where the capital sits (ADR-0007).</summary>
    public static SystemPosition Origin => new(0.0, 0.0, 0.0);

    /// <summary>Distance to another point, in kilometres.</summary>
    public double DistanceTo(SystemPosition other)
    {
        double dx = XKilometres - other.XKilometres;
        double dy = YKilometres - other.YKilometres;
        double dz = ZKilometres - other.ZKilometres;

        return Math.Sqrt((dx * dx) + (dy * dy) + (dz * dz));
    }

    /// <summary>
    /// Dot product with a direction, which is how far along it this point lies.
    /// </summary>
    /// <remarks>
    /// Used to decide which side of the dividing plane a point is on. The sign is the whole
    /// answer; the magnitude is how deep into that half it sits.
    /// </remarks>
    public double Dot(double x, double y, double z) =>
        (XKilometres * x) + (YKilometres * y) + (ZKilometres * z);
}
