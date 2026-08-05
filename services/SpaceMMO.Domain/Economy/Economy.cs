namespace SpaceMMO.Domain.Economy;

/// <summary>
/// Economy-wide constants that belong to no single subsystem.
/// </summary>
public static class Economy
{
    /// <summary>
    /// What a character is created holding. First draft: 500 credits.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>This exists because a character with nothing could not start their first job.</strong>
    /// Every industry job charges a fee — 15 credits for a single run — and creation left the
    /// balance at zero. The onboarding questline was supposed to cover that and does, but only for
    /// a player who follows it, and only because <c>intro_gather_scrap</c> happens to pay out before
    /// the first craft is asked for. Nothing enforced that ordering, and anyone who skipped the
    /// tutorial could mine ore forever without ever being able to refine it.
    /// </para>
    /// <para>
    /// Deliberately the same 500 as the first quest's reward. It is a number the design already
    /// uses, it covers roughly thirty single-run jobs, and it means a player who ignores the
    /// questline is one quest behind rather than locked out. Total bootstrap faucet per character
    /// becomes 13,500 rather than 13,000 (design-bible §4).
    /// </para>
    /// <para>
    /// A stake, not an income: it is paid once, at creation, and there is no path that pays it
    /// again. The dead-end it removes at the <em>start</em> is not the same as the one a player can
    /// reach later by spending down to zero — that one is closed by faction buy orders, which are a
    /// standing way to turn gathered material into credits with nothing paid up front.
    /// </para>
    /// </remarks>
    public static Credits StartingStake { get; } = Credits.FromWholeCredits(500);
}
