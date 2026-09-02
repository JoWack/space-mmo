using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Entities;

namespace SpaceMMO.Data.Whereabouts;

/// <summary>Where a character was, and what they were doing there.</summary>
/// <param name="X">System coordinates, in kilometres.</param>
/// <param name="Flying">Whether they were in a ship rather than stood on something.</param>
public sealed record CharacterWhereabouts(double X, double Y, double Z, bool Flying);

/// <summary>Thrown when a position is not a position: NaN, or infinite.</summary>
/// <remarks>
/// Refused rather than stored, because a poisoned row is only discovered on the far side of a
/// sign-in — as a character placed nowhere, in a client with no way to say why. Every arithmetic
/// path that could produce one is on the game server's side of the wire, which is exactly the side
/// this cannot check.
/// </remarks>
public sealed class ImpossiblePositionException(double x, double y, double z)
    : Exception($"({x}, {y}, {z}) is not a position.")
{
    public double X { get; } = x;

    public double Y { get; } = y;

    public double Z { get; } = z;
}

/// <summary>
/// The last place a character was seen, so signing back in puts them there.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Recorded rather than reserved.</strong> Nothing here holds a place, expires one, or
/// decides where anybody may be — it stores what the simulation reports and hands it back at the
/// next sign-in. The rules about where a player is allowed to be are the game server's, and it is
/// the party that knows.
/// </para>
/// <para>
/// <strong>Written by the game server under the service credential</strong>, for the same reason
/// docking is: this is a position, and a player who could report their own position could report
/// any position, which is a teleport with extra steps (ADR-0003).
/// </para>
/// </remarks>
public sealed class WhereaboutsService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database = database;

    /// <summary>Records where a character is now, replacing whatever was there.</summary>
    /// <exception cref="UnknownCharacterException">If the character does not exist.</exception>
    /// <exception cref="ImpossiblePositionException">If any coordinate is NaN or infinite.</exception>
    public async Task RecordAsync(
        int characterId,
        CharacterWhereabouts where,
        CancellationToken cancellationToken = default)
    {
        if (!double.IsFinite(where.X) || !double.IsFinite(where.Y) || !double.IsFinite(where.Z))
        {
            throw new ImpossiblePositionException(where.X, where.Y, where.Z);
        }

        Character character = await _database.Characters.FindAsync([characterId], cancellationToken)
            ?? throw new UnknownCharacterException(characterId);

        character.LastSystemX = where.X;
        character.LastSystemY = where.Y;
        character.LastSystemZ = where.Z;
        character.LastSeenFlying = where.Flying;

        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>Where this character was last seen, or null if they never have been.</summary>
    /// <remarks>
    /// Null when any coordinate is missing, not when all three are. A row with two of them is a
    /// half-written position, and placing somebody at it would be worse than starting them over:
    /// two coordinates and a zero is a point in space, and it looks exactly like a real one.
    /// </remarks>
    public async Task<CharacterWhereabouts?> LastKnownAsync(
        int characterId, CancellationToken cancellationToken = default)
    {
        var row = await _database.Characters
            .Where(c => c.Id == characterId)
            .Select(c => new
            {
                c.LastSystemX,
                c.LastSystemY,
                c.LastSystemZ,
                c.LastSeenFlying,
            })
            .FirstOrDefaultAsync(cancellationToken);

        if (row is null
            || row.LastSystemX is null
            || row.LastSystemY is null
            || row.LastSystemZ is null)
        {
            return null;
        }

        return new CharacterWhereabouts(
            row.LastSystemX.Value,
            row.LastSystemY.Value,
            row.LastSystemZ.Value,
            row.LastSeenFlying);
    }
}
