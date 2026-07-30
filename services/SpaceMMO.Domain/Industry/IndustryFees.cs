using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Domain.Industry;

/// <summary>
/// What starting a manufacturing job costs, per economy-design §3.
/// </summary>
/// <remarks>
/// <para>
/// A flat charge per job plus a charge per run. The flat part prices the act of occupying a
/// slot; the per-run part ties the sink to actual production volume, so mass manufacturing
/// contributes proportionally more than tinkering does.
/// </para>
/// <para>
/// Charged at start and never refunded, including on cancellation — the same reasoning as the
/// market broker fee, since churn has to cost something.
/// </para>
/// <para>
/// <strong>Known weakness:</strong> both components are absolute rather than proportional to
/// output value, so this sink loses relevance as the economy grows, in a way the percentage-based
/// market fees do not. It is kept simple because balancing it needs EconSim data that does not
/// exist yet; expect it to become value-proportional later.
/// </para>
/// </remarks>
public static class IndustryFees
{
    /// <summary>Flat charge per job, regardless of size. First draft: 10 credits.</summary>
    public static Credits DefaultBaseFee { get; } = Credits.FromWholeCredits(10);

    /// <summary>Additional charge per run. First draft: 5 credits.</summary>
    public static Credits DefaultPerRunFee { get; } = Credits.FromWholeCredits(5);

    /// <summary>
    /// Fee for a job of <paramref name="runs"/> runs.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If runs is not positive.</exception>
    public static Credits ForJob(int runs) => ForJob(runs, DefaultBaseFee, DefaultPerRunFee);

    /// <summary>
    /// Fee for a job at explicit rates, so EconSim can sweep them.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If runs is not positive or either rate is negative.
    /// </exception>
    public static Credits ForJob(int runs, Credits baseFee, Credits perRunFee)
    {
        if (runs <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(runs), runs, "Runs must be positive.");
        }

        if (baseFee.IsNegative)
        {
            throw new ArgumentOutOfRangeException(nameof(baseFee), baseFee, "Fee cannot be negative.");
        }

        if (perRunFee.IsNegative)
        {
            throw new ArgumentOutOfRangeException(
                nameof(perRunFee), perRunFee, "Fee cannot be negative.");
        }

        return baseFee + (perRunFee * runs);
    }
}
