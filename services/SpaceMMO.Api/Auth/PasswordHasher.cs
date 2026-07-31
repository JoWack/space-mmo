using System.Security.Cryptography;

namespace SpaceMMO.Api.Auth;

/// <summary>
/// Hashes and verifies account passwords with PBKDF2-HMAC-SHA256.
/// </summary>
/// <remarks>
/// <para>
/// PBKDF2 from the BCL rather than a dependency, because it is the strongest option available
/// without pulling a package in, and a wrong answer here is unrecoverable: a leaked table of
/// weakly hashed passwords cannot be un-leaked, and players reuse passwords.
/// </para>
/// <para>
/// The iteration count is stored <em>in</em> the hash string. Raising it later must not
/// invalidate every existing password, so each hash records the cost it was created with and
/// verification uses that, not today's constant.
/// </para>
/// </remarks>
public static class PasswordHasher
{
    /// <summary>OWASP's floor for PBKDF2-HMAC-SHA256, as of 2023.</summary>
    private const int DefaultIterations = 600_000;

    private const int SaltBytes = 16;

    private const int HashBytes = 32;

    /// <summary>Hashes a password, generating a fresh random salt.</summary>
    /// <returns><c>pbkdf2-sha256$iterations$salt$hash</c>, all base64.</returns>
    /// <exception cref="ArgumentException">If the password is empty.</exception>
    public static string Hash(string password)
    {
        ArgumentException.ThrowIfNullOrEmpty(password);

        byte[] salt = RandomNumberGenerator.GetBytes(SaltBytes);

        byte[] hash = Rfc2898DeriveBytes.Pbkdf2(
            password, salt, DefaultIterations, HashAlgorithmName.SHA256, HashBytes);

        return string.Join(
            '$',
            "pbkdf2-sha256",
            DefaultIterations.ToString(System.Globalization.CultureInfo.InvariantCulture),
            Convert.ToBase64String(salt),
            Convert.ToBase64String(hash));
    }

    /// <summary>
    /// Verifies a password against a stored hash.
    /// </summary>
    /// <remarks>
    /// Comparison is fixed-time. A plain <c>==</c> on the hash bytes returns as soon as it finds a
    /// difference, and that timing difference is enough to recover a hash byte by byte.
    /// </remarks>
    /// <returns>False for a malformed stored hash rather than throwing — a corrupt row must read
    /// as a failed login, not as a 500 that tells an attacker the account exists.</returns>
    public static bool Verify(string password, string stored)
    {
        if (string.IsNullOrEmpty(password) || string.IsNullOrEmpty(stored))
        {
            return false;
        }

        string[] parts = stored.Split('$');

        if (parts.Length != 4 || parts[0] != "pbkdf2-sha256")
        {
            return false;
        }

        if (!int.TryParse(
                parts[1],
                System.Globalization.NumberStyles.Integer,
                System.Globalization.CultureInfo.InvariantCulture,
                out int iterations)
            || iterations <= 0)
        {
            return false;
        }

        byte[] salt;
        byte[] expected;

        try
        {
            salt = Convert.FromBase64String(parts[2]);
            expected = Convert.FromBase64String(parts[3]);
        }
        catch (FormatException)
        {
            return false;
        }

        byte[] actual = Rfc2898DeriveBytes.Pbkdf2(
            password, salt, iterations, HashAlgorithmName.SHA256, expected.Length);

        return CryptographicOperations.FixedTimeEquals(actual, expected);
    }
}
