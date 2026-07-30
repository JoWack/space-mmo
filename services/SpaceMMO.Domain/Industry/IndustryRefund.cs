namespace SpaceMMO.Domain.Industry;

/// <summary>
/// What a cancelled job returns, per design-bible §6.
/// </summary>
/// <remarks>
/// <para>
/// Inputs are consumed when a job starts, so cancelling has to decide what comes back. The
/// refund is proportional to the time <em>remaining</em>: cancel moments after starting and you
/// get essentially everything back, cancel near completion and you get almost nothing.
/// </para>
/// <para>
/// That single rule covers both cases worth caring about. A misclick caught immediately is
/// forgiven without needing a special grace period, and a job cannot be used as a free option on
/// the output price — backing out late costs nearly the full inputs. The material consumed
/// scales with work actually done, which is also simply what players expect.
/// </para>
/// <para>
/// The job fee is never refunded, on the same reasoning as the market broker fee: churn has to
/// cost something.
/// </para>
/// </remarks>
public static class IndustryRefund
{
    /// <summary>
    /// Units of one input returned when a job is cancelled.
    /// </summary>
    /// <param name="consumedQuantity">Units of this input taken when the job started.</param>
    /// <param name="elapsedSeconds">Seconds since the job started.</param>
    /// <param name="totalSeconds">The job's full duration.</param>
    /// <remarks>
    /// <para>
    /// Integer arithmetic throughout, with no floating point, so the result is identical on every
    /// machine and EconSim can reproduce a run exactly.
    /// </para>
    /// <para>
    /// Rounds half <em>up</em>, which is the opposite of the money rule (ADR-0005) and safe for a
    /// different reason: the refund is capped by what was consumed, so it can never return more
    /// material than it took and therefore cannot be a faucet. Rounding down instead would make
    /// single-unit inputs all-or-nothing — a hull section cancelled at 5% progress would round to
    /// zero and simply vanish, which reads as a bug however it is documented.
    /// </para>
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If the consumed quantity is negative, elapsed time is negative, or the duration is not
    /// positive.
    /// </exception>
    public static int RefundedQuantity(int consumedQuantity, long elapsedSeconds, long totalSeconds)
    {
        if (consumedQuantity < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(consumedQuantity), consumedQuantity, "Consumed quantity cannot be negative.");
        }

        if (elapsedSeconds < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(elapsedSeconds), elapsedSeconds, "Elapsed time cannot be negative.");
        }

        if (totalSeconds <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(totalSeconds), totalSeconds, "Job duration must be positive.");
        }

        if (consumedQuantity == 0)
        {
            return 0;
        }

        // A job past its completion time refunds nothing — at that point it should be claimed,
        // not cancelled.
        long remaining = Math.Max(0L, totalSeconds - elapsedSeconds);

        if (remaining == 0L)
        {
            return 0;
        }

        // round(consumed * remaining / total), computed as (2ab + t) / 2t to keep the halving
        // exact without touching floating point.
        long refunded = ((2L * consumedQuantity * remaining) + totalSeconds) / (2L * totalSeconds);

        // Cannot exceed what was taken. This is what makes rounding up safe.
        return (int)Math.Min(refunded, consumedQuantity);
    }

    /// <summary>
    /// True if a job is far enough along that cancelling returns nothing.
    /// </summary>
    /// <remarks>
    /// Useful for warning a player before they throw materials away for no gain.
    /// </remarks>
    public static bool RefundIsWorthless(
        int consumedQuantity, long elapsedSeconds, long totalSeconds) =>
        RefundedQuantity(consumedQuantity, elapsedSeconds, totalSeconds) == 0;

    /// <summary>
    /// Total seconds a job takes, given its recipe duration and run count.
    /// </summary>
    /// <remarks>
    /// Linear in runs: there is no batch discount. Batching is a convenience so players click
    /// less, not a throughput advantage — an economy of scale would favour established
    /// industrialists over new ones and add a second number to balance on every recipe.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If either argument is not positive.</exception>
    public static long TotalJobSeconds(int recipeSeconds, int runs)
    {
        if (recipeSeconds <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(recipeSeconds), recipeSeconds, "Recipe duration must be positive.");
        }

        if (runs <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(runs), runs, "Runs must be positive.");
        }

        return (long)recipeSeconds * runs;
    }
}
