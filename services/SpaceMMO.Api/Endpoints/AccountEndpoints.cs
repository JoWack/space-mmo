using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;

namespace SpaceMMO.Api.Endpoints;

public sealed record RegisterRequest(string Email, string Password);

public sealed record LoginRequest(string Email, string Password);

public sealed record SessionResponse(int AccountId, string Token, DateTimeOffset ExpiresAt);

/// <summary>What the game server asks about a connecting player.</summary>
public sealed record ResolveCharacterRequest(string Token, int CharacterId);

/// <summary>Who the game server may treat that connection as.</summary>
public sealed record ResolvedCharacter(int AccountId, int CharacterId, string CharacterName);

/// <summary>
/// Account registration and login.
/// </summary>
public static class AccountEndpoints
{
    /// <summary>
    /// Shortest password accepted.
    /// </summary>
    /// <remarks>
    /// Length only. Composition rules ("one symbol, one digit") push people toward predictable
    /// substitutions and measurably weaker passwords, which is why NIST stopped recommending them.
    /// </remarks>
    private const int MinimumPasswordLength = 12;

    public static void MapAccountEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/accounts").WithTags("Accounts");

        group.MapPost("/register", RegisterAsync);
        group.MapPost("/login", LoginAsync);
        group.MapPost("/resolve-character", ResolveCharacterAsync);
    }

    /// <summary>
    /// Answers the one question the game server has about a connecting player: may this connection
    /// act as this character?
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>The token is the proof, and the character id is only a claim.</strong> A connecting
    /// client says "I am character 7"; that is worth nothing on its own, since character ids are
    /// sequential integers and anyone can name one. What a client cannot forge is a signed session
    /// token, so the server sends both here and this reports the character only when the token
    /// really does belong to the account that owns it.
    /// </para>
    /// <para>
    /// <strong>Service credential required.</strong> Not because the answer is secret — a player
    /// could learn all of this with their own token — but because this is the game server's
    /// question, and letting anyone ask it turns the endpoint into a way to test whether a stolen
    /// token is still live without touching anything that would show up in a player's own history.
    /// </para>
    /// <para>
    /// The token travels in the body rather than the URL, because URLs end up in access logs,
    /// proxy logs and browser history. It is still a bearer secret in transit, so this needs TLS
    /// before anything is exposed beyond localhost — the same caveat that already applies to login.
    /// </para>
    /// </remarks>
    private static async Task<IResult> ResolveCharacterAsync(
        ResolveCharacterRequest request,
        HttpContext context,
        ServiceCredential service,
        SessionTokens tokens,
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        if (!service.IsServiceCaller(context))
        {
            return Results.Unauthorized();
        }

        int? accountId = tokens.Validate(request.Token);

        if (accountId is null)
        {
            return Results.Unauthorized();
        }

        Character? character = await database.Characters.SingleOrDefaultAsync(
            c => c.Id == request.CharacterId && c.AccountId == accountId.Value, cancellation);

        // NotFound rather than Forbidden for somebody else's character, exactly as Caller does.
        // A distinct answer here would tell a caller which character ids exist.
        return character is null
            ? Results.NotFound()
            : Results.Ok(new ResolvedCharacter(accountId.Value, character.Id, character.Name));
    }

    private static async Task<IResult> RegisterAsync(
        RegisterRequest request,
        SpaceMmoDbContext database,
        SessionTokens tokens,
        CancellationToken cancellation)
    {
        if (string.IsNullOrWhiteSpace(request.Email) || !request.Email.Contains('@', StringComparison.Ordinal))
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["email"] = ["A valid email address is required."],
            });
        }

        if (request.Password is null || request.Password.Length < MinimumPasswordLength)
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["password"] = [$"Password must be at least {MinimumPasswordLength} characters."],
            });
        }

        string email = request.Email.Trim().ToLowerInvariant();

        if (await database.Accounts.AnyAsync(a => a.Email == email, cancellation))
        {
            // Deliberately the same shape as any other conflict, and it does leak that the address
            // is taken. Registration always does — an address that cannot be registered twice is
            // observable no matter how the response is worded — so the honest error is better than
            // a misleading one. Login, where it would matter more, does not distinguish.
            return Results.Conflict(new { error = "That email address is already registered." });
        }

        var account = new Account
        {
            Email = email,
            PasswordHash = PasswordHasher.Hash(request.Password),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        database.Accounts.Add(account);
        await database.SaveChangesAsync(cancellation);

        return Results.Ok(NewSession(account.Id, tokens));
    }

    private static async Task<IResult> LoginAsync(
        LoginRequest request,
        SpaceMmoDbContext database,
        SessionTokens tokens,
        CancellationToken cancellation)
    {
        string email = (request.Email ?? string.Empty).Trim().ToLowerInvariant();

        Account? account = await database.Accounts
            .SingleOrDefaultAsync(a => a.Email == email, cancellation);

        // Verify even when the account is missing, against a hash that cannot match. Returning
        // early would make a missing account measurably faster than a wrong password, and that
        // timing difference is a free account-enumeration oracle.
        string hash = account?.PasswordHash ?? DummyHash.Value;

        bool ok = PasswordHasher.Verify(request.Password ?? string.Empty, hash);

        if (!ok || account is null)
        {
            return Results.Unauthorized();
        }

        return Results.Ok(NewSession(account.Id, tokens));
    }

    private static SessionResponse NewSession(int accountId, SessionTokens tokens) =>
        new(accountId, tokens.Issue(accountId), DateTimeOffset.UtcNow.Add(SessionTokens.Lifetime));

    /// <summary>
    /// A real hash of a value nobody can log in with, so a login for a nonexistent account costs
    /// the same PBKDF2 work as a real one.
    /// </summary>
    private static class DummyHash
    {
        internal static string Value { get; } =
            PasswordHasher.Hash(Convert.ToBase64String(
                System.Security.Cryptography.RandomNumberGenerator.GetBytes(32)));
    }
}
