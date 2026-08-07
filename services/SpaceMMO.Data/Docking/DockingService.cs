using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;

namespace SpaceMMO.Data.Docking;

/// <summary>Thrown when a character does not exist.</summary>
public sealed class UnknownCharacterException(int characterId)
    : Exception($"No character {characterId}.")
{
    public int CharacterId { get; } = characterId;
}

/// <summary>Thrown when a station does not exist, or has nowhere to dock.</summary>
public sealed class UnknownStationException(int stationId)
    : Exception($"No station {stationId}.")
{
    public int StationId { get; } = stationId;
}

/// <summary>
/// Where a character is docked.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Proximity is checked by the game server, not here.</strong> This records the fact and
/// enforces what the database can know: that the station exists and has a position to be next to.
/// Whether the ship was actually alongside it is a question about where things are in the world,
/// and the only party that knows that is the simulation — which is why docking is called with the
/// service credential, exactly as gathering is.
/// </para>
/// <para>
/// That split is deliberate rather than lazy. The API could be handed a position and check it, but
/// then it would be trusting a position from a caller, and the caller for a player action is
/// ultimately the player. The game server already knows where every ship is because it moved them.
/// </para>
/// </remarks>
public sealed class DockingService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database = database;

    /// <summary>Docks a character at a station, replacing any previous docking.</summary>
    /// <exception cref="UnknownCharacterException">If the character does not exist.</exception>
    /// <exception cref="UnknownStationException">If the station does not exist or has no position.</exception>
    public async Task DockAsync(
        int characterId, int stationId, CancellationToken cancellationToken = default)
    {
        Character character = await _database.Characters.FindAsync(
            [characterId], cancellationToken)
            ?? throw new UnknownCharacterException(characterId);

        Station station = await _database.Stations.FindAsync([stationId], cancellationToken)
            ?? throw new UnknownStationException(stationId);

        // A station with no position cannot be docked at, however close anybody claims to be.
        // Without this, an unplaced station is one every character in the system is next to,
        // because "nowhere" compares equal to wherever the caller says they are.
        bool placed = station.DirectionX is not null || station.SystemX is not null;

        if (!placed)
        {
            throw new UnknownStationException(stationId);
        }

        // Replaces rather than refuses. Flying from one station to another and docking is an
        // ordinary thing to do, and making it an error would mean every client had to undock
        // first or handle a failure that means nothing to a player.
        character.DockedStationId = stationId;

        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>Undocks a character. Doing this when not docked is not an error.</summary>
    public async Task UndockAsync(int characterId, CancellationToken cancellationToken = default)
    {
        Character character = await _database.Characters.FindAsync(
            [characterId], cancellationToken)
            ?? throw new UnknownCharacterException(characterId);

        // Idempotent on purpose. Undocking is what happens when a ship leaves, and a ship can
        // leave in ways nobody sends a message about — a disconnect, a crash, a server restart.
        // Every one of those eventually produces a second undock, and none of them is a fault.
        character.DockedStationId = null;

        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>Where this character is docked, or null.</summary>
    public async Task<int?> DockedStationIdAsync(
        int characterId, CancellationToken cancellationToken = default) =>
        await _database.Characters
            .Where(c => c.Id == characterId)
            .Select(c => c.DockedStationId)
            .FirstOrDefaultAsync(cancellationToken);

    /// <summary>
    /// Whether this character is docked at exactly this station.
    /// </summary>
    /// <remarks>
    /// What every service that needs a station will ask. Deliberately not "is docked anywhere":
    /// a character docked at Grimhold has no business trading on a Terra order book, and a check
    /// that only asked whether they were docked at all would let them.
    /// </remarks>
    public async Task<bool> IsDockedAtAsync(
        int characterId, int stationId, CancellationToken cancellationToken = default) =>
        await DockedStationIdAsync(characterId, cancellationToken) == stationId;
}
