using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;

namespace SpaceMMO.Api.Auth;

/// <summary>
/// Resolves who is calling, and what they are allowed to touch.
/// </summary>
/// <remarks>
/// <para>
/// The single place authorization happens. Every gameplay endpoint takes a character id from the
/// request, and a character id from a client is a claim, not a fact — without a check, any
/// logged-in account could gather with, spend from, or sell the inventory of any character in
/// the game simply by sending a different number.
/// </para>
/// <para>
/// So it is deliberately awkward to skip: endpoints receive their character through
/// <see cref="OwnedCharacterAsync"/> rather than loading one themselves.
/// </para>
/// </remarks>
public sealed class Caller(
    SpaceMmoDbContext database, SessionTokens tokens, ServiceCredential service)
{
    private readonly SpaceMmoDbContext _database = database;
    private readonly SessionTokens _tokens = tokens;
    private readonly ServiceCredential _service = service;

    /// <summary>Account id from the bearer token, or null if unauthenticated.</summary>
    public int? AccountId(HttpContext context)
    {
        string? header = context.Request.Headers.Authorization;

        if (string.IsNullOrEmpty(header))
        {
            return null;
        }

        const string Scheme = "Bearer ";

        return header.StartsWith(Scheme, StringComparison.OrdinalIgnoreCase)
            ? _tokens.Validate(header[Scheme.Length..].Trim())
            : null;
    }

    /// <summary>
    /// Loads a character, but only if the caller owns it.
    /// </summary>
    /// <remarks>
    /// A character belonging to someone else reports as <see cref="OwnershipResult.NotFound"/>,
    /// not as forbidden. Distinguishing the two would turn this endpoint into an oracle for which
    /// character ids exist, which is information the caller has no business having.
    /// </remarks>
    /// <summary>
    /// Loads a character on behalf of the game server, which owns nobody's account.
    /// </summary>
    /// <remarks>
    /// Only for outcomes the simulation decides — gathering is the one today. The character still
    /// has to exist; what changes is where the authority comes from. See
    /// <see cref="ServiceCredential"/> for why the game server needs one at all.
    ///
    /// Falls back to the ordinary ownership check when no service credential is presented, so a
    /// player's own client keeps working and nothing has to know which kind of caller it is.
    /// </remarks>
    public async Task<OwnershipResult> ServiceOrOwnedCharacterAsync(
        HttpContext context, int characterId, CancellationToken cancellation = default)
    {
        if (!_service.IsServiceCaller(context))
        {
            return await OwnedCharacterAsync(context, characterId, cancellation);
        }

        Character? character = await _database.Characters
            .SingleOrDefaultAsync(c => c.Id == characterId, cancellation);

        return character is null ? OwnershipResult.NotFound : OwnershipResult.Owned(character);
    }

    public async Task<OwnershipResult> OwnedCharacterAsync(
        HttpContext context, int characterId, CancellationToken cancellation = default)
    {
        int? accountId = AccountId(context);

        if (accountId is null)
        {
            return OwnershipResult.Unauthenticated;
        }

        Character? character = await _database.Characters
            .SingleOrDefaultAsync(
                c => c.Id == characterId && c.AccountId == accountId.Value, cancellation);

        return character is null ? OwnershipResult.NotFound : OwnershipResult.Owned(character);
    }
}

/// <summary>Outcome of an ownership check.</summary>
public readonly record struct OwnershipResult(Character? Character, OwnershipStatus Status)
{
    public static OwnershipResult Unauthenticated { get; } =
        new(null, OwnershipStatus.Unauthenticated);

    public static OwnershipResult NotFound { get; } = new(null, OwnershipStatus.NotFound);

    public static OwnershipResult Owned(Character character) =>
        new(character, OwnershipStatus.Owned);

    /// <summary>Maps a failed check to its response. Never call on a successful one.</summary>
    /// <exception cref="InvalidOperationException">If the check actually succeeded.</exception>
    public IResult ToProblem() => Status switch
    {
        OwnershipStatus.Unauthenticated => Results.Unauthorized(),
        OwnershipStatus.NotFound => Results.NotFound(),
        _ => throw new InvalidOperationException("Ownership check succeeded; there is no problem."),
    };
}

public enum OwnershipStatus
{
    Unauthenticated,
    NotFound,
    Owned,
}
