using SpaceMMO.Domain.Economy;

namespace SpaceMMO.EconSim;

/// <summary>A violated economic invariant.</summary>
public sealed record InvariantViolation(int Day, string Invariant, string Detail)
{
    public override string ToString() => $"day {Day}: {Invariant} — {Detail}";
}

/// <summary>
/// The economic invariants from economy-design §4, checked continuously.
/// </summary>
/// <remarks>
/// These are the same properties the integration tests assert, but asserted every simulated day
/// across millions of transactions rather than in a handful of scenarios. Invariants 1 to 3 are
/// correctness and must never fail; the rest are balance signals, reported rather than enforced.
/// </remarks>
public static class Invariants
{
    /// <summary>Checks every conservation property and returns anything broken.</summary>
    public static List<InvariantViolation> Check(SimWorld world, int day)
    {
        ArgumentNullException.ThrowIfNull(world);

        var violations = new List<InvariantViolation>();

        CheckMaterialConservation(world, day, violations);
        CheckLedgerConservation(world, day, violations);
        CheckFaucetAttribution(world, day, violations);
        CheckNoNegativeBalances(world, day, violations);

        return violations;
    }

    /// <summary>
    /// Every unit is accounted for: gathered plus crafted, minus destroyed, equals what is held
    /// plus what is sitting on the book.
    /// </summary>
    /// <remarks>
    /// Invariant 1, and the one that matters most. Any drift is either a dupe or a leak, and in a
    /// player-driven economy both are fatal.
    /// </remarks>
    private static void CheckMaterialConservation(
        SimWorld world, int day, List<InvariantViolation> violations)
    {
        foreach (string item in Sim.TradedItems)
        {
            long created = world.Gathered.GetValueOrDefault(item)
                + world.Crafted.GetValueOrDefault(item);

            long destroyed = world.Destroyed.GetValueOrDefault(item);
            long held = world.Characters.Sum(c => (long)c.Held(item));
            long onBook = world.GoodsOnBook(item);

            if (created - destroyed != held + onBook)
            {
                violations.Add(new InvariantViolation(
                    day,
                    "material conservation",
                    $"{item}: created {created} - destroyed {destroyed} = {created - destroyed}, "
                    + $"but held {held} + on book {onBook} = {held + onBook}."));
            }
        }
    }

    /// <summary>
    /// The ledger sums to the balances it describes.
    /// </summary>
    /// <remarks>
    /// Invariant 2. Escrowed credits are deliberately excluded: they have left their owner's
    /// balance and live on the order, which is exactly what the ledger records.
    /// </remarks>
    private static void CheckLedgerConservation(
        SimWorld world, int day, List<InvariantViolation> violations)
    {
        long ledger = world.Ledger.Sum(e => e.Delta.MinorUnits);
        long balances = world.Characters.Sum(c => c.Balance.MinorUnits);

        if (ledger != balances)
        {
            violations.Add(new InvariantViolation(
                day,
                "ledger conservation",
                $"ledger sums to {ledger} but balances total {balances} (difference {ledger - balances})."));
        }
    }

    /// <summary>
    /// The money supply equals what faucets created minus what sinks destroyed.
    /// </summary>
    /// <remarks>
    /// Invariants 3 and 6 together. Supply is balances plus escrow, because escrowed credits still
    /// exist. Transfers cancel out by construction — if they do not, something is creating credits
    /// while claiming to move them, which is precisely how an economy inflates unnoticed.
    /// </remarks>
    private static void CheckFaucetAttribution(
        SimWorld world, int day, List<InvariantViolation> violations)
    {
        long faucets = 0;
        long sinks = 0;

        foreach ((_, Credits delta, LedgerReason reason) in world.Ledger)
        {
            switch (LedgerReasons.KindOf(reason))
            {
                case LedgerReasonKind.Faucet:
                    faucets += delta.MinorUnits;
                    break;

                case LedgerReasonKind.Sink:
                    sinks += -delta.MinorUnits;
                    break;

                case LedgerReasonKind.Transfer:
                    break;

                default:
                    violations.Add(new InvariantViolation(
                        day, "faucet attribution", $"unclassified ledger reason '{reason}'."));
                    break;
            }
        }

        long supply = world.Characters.Sum(c => c.Balance.MinorUnits) + world.Escrow.MinorUnits;

        if (supply != faucets - sinks)
        {
            violations.Add(new InvariantViolation(
                day,
                "money supply",
                $"supply {supply} but faucets {faucets} - sinks {sinks} = {faucets - sinks}."));
        }
    }

    /// <summary>
    /// Nobody spends credits they do not have.
    /// </summary>
    /// <remarks>
    /// Not in the design document, but a negative balance means a spending path skipped its
    /// affordability check — and that is a credit faucet wearing a disguise.
    /// </remarks>
    private static void CheckNoNegativeBalances(
        SimWorld world, int day, List<InvariantViolation> violations)
    {
        foreach (SimCharacter character in world.Characters.Where(c => c.Balance.IsNegative))
        {
            violations.Add(new InvariantViolation(
                day,
                "no negative balances",
                $"character {character.Id} ({character.Archetype}) is at {character.Balance}."));
        }
    }
}
