namespace SpaceMMO.Domain.Random;

/// <summary>
/// Hashes that are stable across processes, machines, and runtime versions.
/// </summary>
/// <remarks>
/// <see cref="string.GetHashCode()"/> is randomized per process in .NET, so it must never
/// be used to derive anything reproducible. Death resolution seeds items by their key
/// (ADR-0006), so it needs a hash whose value is fixed forever.
/// </remarks>
public static class StableHash
{
    private const ulong Fnv1aOffsetBasis = 0xCBF2_9CE4_8422_2325UL;
    private const ulong Fnv1aPrime = 0x0000_0100_0000_01B3UL;

    /// <summary>
    /// FNV-1a over the UTF-16 code units of <paramref name="value"/>.
    /// </summary>
    /// <remarks>
    /// Not cryptographic, and not collision-resistant against an adversary. It is used
    /// only to decorrelate per-item random streams, where a collision would mean two item
    /// keys share a roll sequence — harmless.
    /// </remarks>
    public static ulong Fnv1a64(string value)
    {
        ArgumentNullException.ThrowIfNull(value);

        unchecked
        {
            ulong hash = Fnv1aOffsetBasis;

            foreach (char c in value)
            {
                // Hash both bytes of each code unit so that strings differing only in the
                // high byte do not collide.
                hash = (hash ^ (byte)c) * Fnv1aPrime;
                hash = (hash ^ (byte)(c >> 8)) * Fnv1aPrime;
            }

            return hash;
        }
    }

    /// <summary>
    /// Combines two 64-bit values into one, using the SplitMix64 finalizer for avalanche.
    /// </summary>
    public static ulong Combine(ulong left, ulong right)
    {
        unchecked
        {
            ulong z = left ^ (right + 0x9E37_79B9_7F4A_7C15UL);
            z = (z ^ (z >> 30)) * 0xBF58_476D_1CE4_E5B9UL;
            z = (z ^ (z >> 27)) * 0x94D0_49BB_1331_11EBUL;

            return z ^ (z >> 31);
        }
    }
}
