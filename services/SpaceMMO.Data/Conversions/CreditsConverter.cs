using Microsoft.EntityFrameworkCore.Storage.ValueConversion;
using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Data.Conversions;

/// <summary>
/// Maps <see cref="Credits"/> to a Postgres <c>bigint</c> of minor units.
/// </summary>
/// <remarks>
/// The stored value is exactly <see cref="Credits.MinorUnits"/> — no scaling, no rounding, no
/// floating point anywhere in the round trip (ADR-0005). Registered as a convention in
/// <see cref="SpaceMmoDbContext.ConfigureConventions"/> so it applies to every
/// <see cref="Credits"/> property automatically, including nullable ones. Registering it
/// per-property would eventually miss one, and a money column stored as anything other than
/// int64 minor units is a correctness bug rather than a style problem.
/// </remarks>
public sealed class CreditsConverter : ValueConverter<Credits, long>
{
    public CreditsConverter()
        : base(credits => credits.MinorUnits, minorUnits => Credits.FromMinorUnits(minorUnits))
    {
    }
}
