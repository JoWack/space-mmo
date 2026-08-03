namespace SpaceMMO.Api.Auth;

/// <summary>
/// The game server's own credential, distinct from any player's session.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Why this exists.</strong> ADR-0003 says the server owns every outcome and the client only
/// ever sends intent. Gathering is the sharpest case: whether a player is standing next to a deposit
/// is a fact about the simulation, and only the Unreal dedicated server knows it. But that server is
/// not a player and holds no player's token, so without a credential of its own it cannot act — and
/// the alternative is letting each client call the gathering endpoint directly, which puts the range
/// check on the machine with the motive to skip it. In a player-driven economy that is not a cheat,
/// it is a material faucet with no upper bound.
/// </para>
/// <para>
/// <strong>What it is not.</strong> Not a superuser, and not a way around ownership. A service caller
/// still names one character and the request still resolves to that character; the difference is only
/// that the caller's authority comes from being the game server rather than from owning the account.
/// It cannot read one player's inventory into another's, because nothing here grants reads at all.
/// </para>
/// <para>
/// <strong>Absent by default.</strong> With no secret configured, this rejects everything rather than
/// accepting everything — the failure mode of a missing secret must be a server that cannot gather,
/// not a server anyone can impersonate. Tests and local play set one explicitly.
/// </para>
/// </remarks>
public sealed class ServiceCredential
{
    private readonly byte[]? _secret;

    public ServiceCredential(IConfiguration configuration)
    {
        string? secret = configuration["SpaceMMO:ServiceSecret"];

        _secret = string.IsNullOrWhiteSpace(secret)
            ? null
            : System.Text.Encoding.UTF8.GetBytes(secret);
    }

    /// <summary>Header the game server presents. Deliberately not <c>Authorization</c>.</summary>
    /// <remarks>
    /// A separate header so a service call can never be confused for a player session by a future
    /// reader of this code, and so logging that redacts one does not silently miss the other.
    /// </remarks>
    public const string HeaderName = "X-SpaceMMO-Service";

    public bool IsConfigured => _secret is not null;

    /// <summary>Whether a request carries the game server's credential.</summary>
    public bool IsServiceCaller(HttpContext context)
    {
        if (_secret is null)
        {
            return false;
        }

        if (!context.Request.Headers.TryGetValue(HeaderName, out Microsoft.Extensions.Primitives.StringValues values))
        {
            return false;
        }

        string? presented = values.ToString();

        if (string.IsNullOrEmpty(presented))
        {
            return false;
        }

        // Fixed-time comparison. A secret compared with ordinary string equality leaks its prefix
        // through timing, and this one is worth as much as every character in the game.
        return System.Security.Cryptography.CryptographicOperations.FixedTimeEquals(
            System.Text.Encoding.UTF8.GetBytes(presented), _secret);
    }
}
