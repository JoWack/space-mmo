using System.Globalization;

namespace SpaceMMO.Domain.Economy;

/// <summary>
/// A quantity of in-game currency, stored as int64 minor units (ADR-0005).
/// </summary>
/// <remarks>
/// <para>
/// This type exists so that a raw <see cref="long"/> can never be assigned to money by
/// accident, and so that unit confusion — whole credits versus minor units — is a
/// compile error rather than an off-by-100 bug in a market transaction.
/// </para>
/// <para>
/// There is deliberately <em>no</em> conversion from any floating-point type. Percentage
/// calculations take basis points as an <see cref="int"/>, and rounding direction is
/// always chosen explicitly by the caller — see
/// <see cref="PercentRoundedUp"/> and <see cref="PercentRoundedDown"/>.
/// </para>
/// </remarks>
public readonly record struct Credits : IComparable<Credits>, IComparable
{
    /// <summary>Minor units in one whole credit. Fixed forever; see ADR-0005.</summary>
    public const long MinorUnitsPerCredit = 100L;

    /// <summary>One hundred percent, expressed in basis points.</summary>
    public const int OneHundredPercentBasisPoints = 10_000;

    /// <summary>Zero credits.</summary>
    public static readonly Credits Zero = new(0L);

    /// <summary>The raw stored value, in minor units. This is what goes in the database.</summary>
    public long MinorUnits { get; }

    private Credits(long minorUnits) => MinorUnits = minorUnits;

    /// <summary>Wraps a raw minor-unit value, as read from the database or the wire.</summary>
    public static Credits FromMinorUnits(long minorUnits) => new(minorUnits);

    /// <summary>
    /// Builds an amount from whole credits, the form used in design documents and
    /// content JSON.
    /// </summary>
    /// <exception cref="OverflowException">If the result would exceed int64.</exception>
    public static Credits FromWholeCredits(long wholeCredits) =>
        new(checked(wholeCredits * MinorUnitsPerCredit));

    /// <summary>True if this amount is exactly zero.</summary>
    public bool IsZero => MinorUnits == 0L;

    /// <summary>True if this amount is greater than zero.</summary>
    public bool IsPositive => MinorUnits > 0L;

    /// <summary>True if this amount is less than zero. Valid for ledger deltas, not balances.</summary>
    public bool IsNegative => MinorUnits < 0L;

    /// <summary>
    /// Applies a basis-point rate, rounding <em>up</em>. Use for anything charged to a
    /// player — fees, premiums, taxes — so rounding is a sink of at most one minor unit
    /// rather than an exploitable faucet (ADR-0005).
    /// </summary>
    /// <param name="basisPoints">Rate in basis points; 10,000 = 100%.</param>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If <paramref name="basisPoints"/> is negative, or this amount is negative.
    /// </exception>
    public Credits PercentRoundedUp(int basisPoints)
    {
        GuardPercentInputs(basisPoints);

        // Int128 keeps the intermediate product exact. A plain long multiply overflows
        // at around 9.2e14 minor units once scaled by 10,000 basis points, which is a
        // reachable balance in a late-game economy.
        Int128 scaled = (Int128)MinorUnits * basisPoints;
        Int128 rounded = (scaled + (OneHundredPercentBasisPoints - 1)) / OneHundredPercentBasisPoints;

        return new Credits((long)rounded);
    }

    /// <summary>
    /// Applies a basis-point rate, rounding <em>down</em>. Use for anything paid to a
    /// player — insurance payouts, quest rewards, sale proceeds.
    /// </summary>
    /// <param name="basisPoints">Rate in basis points; 10,000 = 100%.</param>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If <paramref name="basisPoints"/> is negative, or this amount is negative.
    /// </exception>
    public Credits PercentRoundedDown(int basisPoints)
    {
        GuardPercentInputs(basisPoints);

        Int128 scaled = (Int128)MinorUnits * basisPoints;

        return new Credits((long)(scaled / OneHundredPercentBasisPoints));
    }

    private void GuardPercentInputs(int basisPoints)
    {
        if (basisPoints < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(basisPoints), basisPoints, "A rate cannot be negative.");
        }

        // Percentages of a negative amount have no well-defined rounding direction —
        // "round up" would favour the player on a debit. Callers must handle sign
        // themselves so the intent is explicit at the call site.
        if (IsNegative)
        {
            throw new InvalidOperationException(
                "Cannot apply a rate to a negative amount; handle the sign explicitly.");
        }
    }

    public static Credits operator +(Credits left, Credits right) =>
        new(checked(left.MinorUnits + right.MinorUnits));

    public static Credits operator -(Credits left, Credits right) =>
        new(checked(left.MinorUnits - right.MinorUnits));

    public static Credits operator -(Credits value) => new(checked(-value.MinorUnits));

    /// <summary>Scales an amount by a whole multiplier, e.g. a per-run industry fee.</summary>
    public static Credits operator *(Credits value, long multiplier) =>
        new(checked(value.MinorUnits * multiplier));

    /// <summary>Scales an amount by a whole multiplier.</summary>
    public static Credits operator *(long multiplier, Credits value) => value * multiplier;

    public static bool operator <(Credits left, Credits right) => left.MinorUnits < right.MinorUnits;

    public static bool operator >(Credits left, Credits right) => left.MinorUnits > right.MinorUnits;

    public static bool operator <=(Credits left, Credits right) => left.MinorUnits <= right.MinorUnits;

    public static bool operator >=(Credits left, Credits right) => left.MinorUnits >= right.MinorUnits;

    /// <summary>Returns the smaller of two amounts.</summary>
    public static Credits Min(Credits left, Credits right) => left <= right ? left : right;

    /// <summary>Returns the larger of two amounts.</summary>
    public static Credits Max(Credits left, Credits right) => left >= right ? left : right;

    public int CompareTo(Credits other) => MinorUnits.CompareTo(other.MinorUnits);

    public int CompareTo(object? obj) => obj switch
    {
        null => 1,
        Credits other => CompareTo(other),
        _ => throw new ArgumentException($"Object must be of type {nameof(Credits)}.", nameof(obj)),
    };

    /// <summary>
    /// Formats as whole credits with two minor digits, e.g. <c>1,234.50 cr</c>.
    /// </summary>
    /// <remarks>
    /// Formatting is centralized here on purpose. Scattering the divide-by-100 across
    /// call sites guarantees an off-by-100 bug in some UI eventually.
    /// </remarks>
    public override string ToString() =>
        string.Create(
            CultureInfo.InvariantCulture,
            $"{MinorUnits / (decimal)MinorUnitsPerCredit:N2} cr");
}
