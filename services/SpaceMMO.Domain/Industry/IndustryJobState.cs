namespace SpaceMMO.Domain.Industry;

/// <summary>
/// Lifecycle of a time-gated manufacturing job.
/// </summary>
/// <remarks>
/// Inputs are consumed at <see cref="Running"/> and outputs are created at
/// <see cref="Claimed"/>. That ordering matters: consuming late would let a player start
/// many jobs from one set of materials, and creating early would hand them goods before the
/// time cost was paid.
/// </remarks>
public enum IndustryJobState
{
    /// <summary>Inputs consumed, waiting on the server clock to reach the completion time.</summary>
    Running = 0,

    /// <summary>Completed and outputs delivered. Terminal.</summary>
    Claimed = 1,

    /// <summary>
    /// Cancelled before completion. Terminal. Whether inputs are refunded is a balance
    /// decision that is not yet made — a full refund makes job slots free to speculate with.
    /// </summary>
    Cancelled = 2,
}
