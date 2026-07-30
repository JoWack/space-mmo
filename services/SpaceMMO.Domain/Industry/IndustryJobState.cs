namespace SpaceMMO.Domain.Industry;

/// <summary>
/// Lifecycle of a time-gated manufacturing job.
/// </summary>
/// <remarks>
/// <para>
/// Inputs are consumed at <see cref="Running"/> and outputs are created at
/// <see cref="Claimed"/>. That ordering matters: consuming late would let a player start many
/// jobs from one set of materials, and creating early would hand them goods before the time
/// cost was paid.
/// </para>
/// <para>
/// XP is awarded at <see cref="Claimed"/> and never at <see cref="Running"/>. Awarding it at
/// start would make start-and-cancel an XP farm costing only the job fee.
/// </para>
/// </remarks>
public enum IndustryJobState
{
    /// <summary>Inputs consumed, waiting on the server clock to reach the completion time.</summary>
    Running = 0,

    /// <summary>Completed and outputs delivered. Terminal.</summary>
    Claimed = 1,

    /// <summary>
    /// Cancelled before completion. Terminal.
    /// </summary>
    /// <remarks>
    /// Inputs are refunded in proportion to the time remaining — see
    /// <see cref="IndustryRefund.RefundedQuantity"/>. The job fee is not refunded.
    /// </remarks>
    Cancelled = 2,
}
