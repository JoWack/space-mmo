namespace SpaceMMO.Domain.Random;

/// <summary>
/// A deterministic, seedable pseudo-random generator — SplitMix64.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="SpaceMMO.Domain"/> contains no ambient randomness by design: every
/// stochastic outcome is a pure function of an explicit seed the server draws, records,
/// and can replay. That makes destruction outcomes unit-testable, lets EconSim reproduce
/// runs exactly, and turns a player dispute over a lost item into a replay rather than a
/// matter of opinion (ADR-0006).
/// </para>
/// <para>
/// <see cref="System.Random"/> is deliberately not used: its algorithm is not contractually
/// stable across .NET versions, so a runtime upgrade could silently change historical
/// outcomes. SplitMix64 is a fixed sequence of integer operations and will produce the
/// same stream forever, on any platform.
/// </para>
/// <para>
/// This is not cryptographically secure and must never be used for tokens, session
/// identifiers, or anything an attacker benefits from predicting.
/// </para>
/// </remarks>
public struct SplitMix64
{
    private ulong _state;

    /// <summary>Creates a generator from a seed. Any seed value is valid, including zero.</summary>
    public SplitMix64(ulong seed) => _state = seed;

    /// <summary>Returns the next 64 bits and advances the state.</summary>
    public ulong NextUInt64()
    {
        // Deliberate wraparound arithmetic. `unchecked` is required because the build
        // sets CheckForOverflowUnderflow (see services/Directory.Build.props), which
        // would otherwise turn every step of this into an OverflowException.
        unchecked
        {
            _state += 0x9E37_79B9_7F4A_7C15UL;

            ulong z = _state;
            z = (z ^ (z >> 30)) * 0xBF58_476D_1CE4_E5B9UL;
            z = (z ^ (z >> 27)) * 0x94D0_49BB_1331_11EBUL;

            return z ^ (z >> 31);
        }
    }

    /// <summary>
    /// Returns a value in <c>[0, exclusiveUpperBound)</c>.
    /// </summary>
    /// <remarks>
    /// Uses plain modulo, which is very slightly biased toward low values because 2^64
    /// is not a multiple of the bound. For a bound of 100 the bias is on the order of
    /// 1 in 10^17 — irrelevant for loot rolls, and not worth the cost of rejection
    /// sampling. Do not reuse this where exact uniformity matters.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If the bound is not positive.</exception>
    public int NextBelow(int exclusiveUpperBound)
    {
        if (exclusiveUpperBound <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(exclusiveUpperBound), exclusiveUpperBound, "Bound must be positive.");
        }

        return (int)(NextUInt64() % (ulong)exclusiveUpperBound);
    }
}
