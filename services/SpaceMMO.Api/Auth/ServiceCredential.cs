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

    /// <summary>
    /// A short fingerprint of a value, for comparing two machines' secrets in logs.
    /// </summary>
    /// <remarks>
    /// <para>
    /// A fingerprint rather than the value, because logs get pasted into bug reports. Matching the
    /// Unreal client's fingerprint format exactly is the point — without one, "the header did not
    /// arrive" and "the header arrived holding a different value" look identical from either side,
    /// and both present as a plain 401.
    /// </para>
    /// <para>
    /// FNV-1a rather than a cryptographic hash, for two reasons: it is eight lines in any language,
    /// so the C++ side is certain to agree, and it is not pretending to be a security primitive —
    /// this identifies a value, it does not protect one.
    /// </para>
    /// </remarks>
    public static string Fingerprint(string? value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return "<empty>";
        }

        const ulong Offset = 14695981039346656037;
        const ulong Prime = 1099511628211;

        ulong hash = Offset;

        foreach (byte b in System.Text.Encoding.UTF8.GetBytes(value))
        {
            unchecked
            {
                hash ^= b;
                hash *= Prime;
            }
        }

        return hash.ToString("x16", System.Globalization.CultureInfo.InvariantCulture);
    }

    /// <summary>Fingerprint of the configured secret, or <c>&lt;none&gt;</c>.</summary>
    public string ConfiguredFingerprint =>
        _secret is null ? "<none>" : Fingerprint(System.Text.Encoding.UTF8.GetString(_secret));

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
