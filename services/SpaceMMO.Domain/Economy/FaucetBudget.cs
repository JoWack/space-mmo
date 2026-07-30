namespace SpaceMMO.Domain.Economy;

/// <summary>
/// The result of asking to create credits: how much was allowed, and how much was refused.
/// </summary>
/// <param name="Granted">Credits that may actually be created.</param>
/// <param name="Withheld">Credits refused because the daily cap was reached.</param>
public readonly record struct FaucetGrant(Credits Granted, Credits Withheld)
{
    /// <summary>True if the cap reduced the grant.</summary>
    public bool WasCapped => Withheld.IsPositive;

    /// <summary>What was originally asked for.</summary>
    public Credits Requested => Granted + Withheld;
}

/// <summary>
/// The single chokepoint through which credits enter the economy, per economy-design §2b.
/// </summary>
/// <remarks>
/// <para>
/// Every credit faucet routes through here — repeatable sidequests today, and whatever
/// else gets added later. That is the entire point: because the aggregate per-character
/// per-day rate is bounded, <strong>adding a new way to earn credits requires no economic
/// rebalancing</strong>. Without a shared chokepoint, every new faucet is a fresh balance
/// risk and a fresh exploit surface.
/// </para>
/// <para>
/// Two things deliberately do <em>not</em> route through here. One-shot
/// <c>main_story</c> rewards are already bounded at 13,000 credits per character for a
/// whole questline, which is unfarmable. Insurance payouts are exempt by design — losing
/// a capital ship must not be throttled by a daily budget — and are accounted on their
/// own ledger reason (ADR-0006).
/// </para>
/// <para>
/// When the cap bites, the caller should still award XP and items and withhold only the
/// credits. A hard wall that makes content worthless past a threshold teaches players to
/// stop playing at the cap; withholding just the credits keeps the activity worth doing
/// for progression.
/// </para>
/// <para>
/// This type is pure. Tracking what a character has already been granted today, and
/// resetting at the UTC day boundary, belongs to the caller and the
/// <c>character_faucet_daily</c> table.
/// </para>
/// </remarks>
public static class FaucetBudget
{
    /// <summary>
    /// First-draft cap: 5,000 credits per character per UTC day.
    /// </summary>
    /// <remarks>
    /// This is the <c>F</c> in the equilibrium condition <c>F ≈ S</c> that the whole
    /// economy balances around, so it is expected to move once EconSim can measure the
    /// sink rate. It lives here as a default, not as a constant, precisely so it stays
    /// configurable.
    /// </remarks>
    public static Credits DefaultDailyCap { get; } = Credits.FromWholeCredits(5_000);

    /// <summary>
    /// Decides how much of a requested credit grant the daily cap allows.
    /// </summary>
    /// <param name="requested">Credits the source wants to create.</param>
    /// <param name="alreadyGrantedToday">Capped-faucet credits already granted this UTC day.</param>
    /// <param name="dailyCap">The per-character daily ceiling.</param>
    /// <exception cref="ArgumentOutOfRangeException">If any argument is negative.</exception>
    public static FaucetGrant Evaluate(Credits requested, Credits alreadyGrantedToday, Credits dailyCap)
    {
        GuardNonNegative(requested, nameof(requested));
        GuardNonNegative(alreadyGrantedToday, nameof(alreadyGrantedToday));
        GuardNonNegative(dailyCap, nameof(dailyCap));

        // Clamped at zero rather than trusted: a cap that was lowered by a config change
        // can leave a character already over the new limit, and that must not produce a
        // negative remaining budget.
        Credits remaining = Credits.Max(Credits.Zero, dailyCap - alreadyGrantedToday);

        Credits granted = Credits.Min(requested, remaining);

        return new FaucetGrant(granted, requested - granted);
    }

    /// <summary>
    /// Overload using <see cref="DefaultDailyCap"/>.
    /// </summary>
    public static FaucetGrant Evaluate(Credits requested, Credits alreadyGrantedToday) =>
        Evaluate(requested, alreadyGrantedToday, DefaultDailyCap);

    private static void GuardNonNegative(Credits value, string parameterName)
    {
        if (value.IsNegative)
        {
            throw new ArgumentOutOfRangeException(parameterName, value, "Value cannot be negative.");
        }
    }
}
