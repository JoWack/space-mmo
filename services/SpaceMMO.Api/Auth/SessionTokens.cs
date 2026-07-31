using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace SpaceMMO.Api.Auth;

/// <summary>
/// Issues and validates signed session tokens.
/// </summary>
/// <remarks>
/// <para>
/// Stateless and HMAC-signed: the token carries the account id and an expiry, and the signature
/// proves the server issued it. No session table, so no schema change and no database round trip
/// on every request.
/// </para>
/// <para>
/// The trade is that a token cannot be revoked before it expires. That is why the lifetime is
/// short. <strong>Before any public test, this needs a revocation story</strong> — a token
/// version column on the account, bumped on password change, is the usual cheap answer and is
/// worth doing the moment accounts are worth stealing.
/// </para>
/// </remarks>
public sealed class SessionTokens
{
    /// <summary>How long an issued token stays valid.</summary>
    public static readonly TimeSpan Lifetime = TimeSpan.FromHours(12);

    private readonly byte[] _key;
    private readonly TimeProvider _clock;

    /// <param name="signingKey">
    /// Server secret. Must be the same across restarts, or every player is logged out on deploy.
    /// </param>
    /// <exception cref="ArgumentException">If the key is too short to be worth signing with.</exception>
    public SessionTokens(string signingKey, TimeProvider? clock = null)
    {
        ArgumentException.ThrowIfNullOrEmpty(signingKey);

        // 32 bytes of entropy is the floor for HMAC-SHA256. A short key here would make the
        // signature forgeable, and a forged token is an account takeover.
        if (Encoding.UTF8.GetByteCount(signingKey) < 32)
        {
            throw new ArgumentException(
                "Signing key must be at least 32 bytes.", nameof(signingKey));
        }

        _key = Encoding.UTF8.GetBytes(signingKey);
        _clock = clock ?? TimeProvider.System;
    }

    /// <summary>Issues a token for an account.</summary>
    public string Issue(int accountId)
    {
        long expiresAt = _clock.GetUtcNow().Add(Lifetime).ToUnixTimeSeconds();

        string payload = string.Create(
            CultureInfo.InvariantCulture, $"{accountId}.{expiresAt}");

        return $"{Base64Url(Encoding.UTF8.GetBytes(payload))}.{Base64Url(Sign(payload))}";
    }

    /// <summary>
    /// Validates a token and returns the account it belongs to.
    /// </summary>
    /// <remarks>
    /// The signature is checked <em>before</em> the expiry, and both before the payload is
    /// trusted for anything. Reading the account id out of an unverified token and then checking
    /// the signature is the classic way this goes wrong.
    /// </remarks>
    /// <returns>The account id, or null if the token is malformed, forged, or expired.</returns>
    public int? Validate(string? token)
    {
        if (string.IsNullOrEmpty(token))
        {
            return null;
        }

        string[] parts = token.Split('.');

        if (parts.Length != 2)
        {
            return null;
        }

        byte[] payloadBytes;
        byte[] signature;

        try
        {
            payloadBytes = FromBase64Url(parts[0]);
            signature = FromBase64Url(parts[1]);
        }
        catch (FormatException)
        {
            return null;
        }

        string payload = Encoding.UTF8.GetString(payloadBytes);

        if (!CryptographicOperations.FixedTimeEquals(Sign(payload), signature))
        {
            return null;
        }

        string[] fields = payload.Split('.');

        if (fields.Length != 2
            || !int.TryParse(fields[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out int accountId)
            || !long.TryParse(fields[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out long expiresAt))
        {
            return null;
        }

        return _clock.GetUtcNow().ToUnixTimeSeconds() >= expiresAt ? null : accountId;
    }

    private byte[] Sign(string payload) =>
        HMACSHA256.HashData(_key, Encoding.UTF8.GetBytes(payload));

    /// <summary>
    /// Base64url, because a token travels in an <c>Authorization</c> header and standard base64's
    /// <c>+</c>, <c>/</c> and <c>=</c> do not survive every proxy and query string intact.
    /// </summary>
    private static string Base64Url(byte[] value) =>
        Convert.ToBase64String(value).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    private static byte[] FromBase64Url(string value)
    {
        string padded = value.Replace('-', '+').Replace('_', '/');

        return Convert.FromBase64String(padded.PadRight((padded.Length + 3) / 4 * 4, '='));
    }
}
