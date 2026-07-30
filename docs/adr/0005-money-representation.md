# ADR-0005 — Credits as int64 minor units on an append-only ledger

**Status:** Accepted · 2026-07-29

## Context

In a player-driven economy, the currency *is* the game. Players will accumulate wealth
over years, and every bug in money handling is either theft from a player or inflation
inflicted on everyone. Two failure classes have to be designed out rather than tested
out:

- **Representation error.** Binary floating point cannot represent most decimal
  fractions exactly. Repeated arithmetic on `float`/`double` balances accumulates drift,
  and the classic exploit is finding a rounding direction that favors the attacker and
  running it in a loop.
- **Lost history.** If balances are a mutable number, then a dupe bug is undetectable
  after the fact and unfixable — there is no record of what *should* have happened.

## Decision

**Credits are `long`, in minor units. Balances are derived from an append-only ledger.**

1. **`long` minor units.** All prices, balances, and fees are integers. `float` and
   `double` are banned from any code path touching money. `decimal` is permitted only
   in offline analysis, never in storage or on the wire.

2. **Append-only ledger.** `ledger_entries` is insert-only — no updates, no deletes.
   Every entry carries `character_id`, `delta_credits`, a typed `LedgerReason`, and a
   `ref_id` linking to the causing trade, job, or quest.

3. **Balances are derived.** A materialized balance column may exist as a cache, but
   the ledger is authoritative. A reconciliation job asserts they agree, and when they
   disagree, the ledger wins.

4. **Rounding is explicit and always against the actor.** Any calculation producing a
   fraction (percentage fees, split fills) rounds in the direction that does not favor
   the player initiating the action. Fees round up, payouts round down. This makes
   rounding a sink of at most one minor unit rather than an exploitable faucet.

5. **Every fee has a defined `LedgerReason`**, so EconSim can attribute all credit
   creation and destruction.

## Consequences

Positive:

- Exact arithmetic. Sums are associative, comparisons are reliable, and no drift ever
  accumulates.
- `long` holds ±9.2 × 10^18 minor units — ample headroom even for a hyperinflated
  late-game economy, with no overflow risk at realistic scales.
- The ledger makes every dupe bug *auditable after the fact*. Wealth can be traced to
  its origin and a bad transaction can be reversed with a compensating entry.
- Faucet and sink attribution is a `GROUP BY` over the ledger, which is what makes the
  EconSim invariants checkable at all.

Negative, and accepted:

- The ledger grows without bound and becomes one of the largest tables in the database.
  It needs partitioning by time, and it must never be pruned — only archived.
- Deriving balances requires either a cache or an aggregate query, so there are two
  representations to keep honest. The reconciliation job is mandatory, not optional.
- Every display site must divide by the minor-unit factor. Formatting has to be
  centralized, because scattering it guarantees an off-by-100 bug in some UI.
- Never mutating a row is a discipline that resists ORM conveniences; EF Core makes
  updating an entity the path of least resistance.

## Consequences for code

- One `Credits` value type wrapping `long` in `SpaceMMO.Domain`, so a raw `long` can
  never be assigned to money by accident and units are enforced by the type system.
- No implicit conversion from any floating-point type. Percentage fees take the rate as
  basis points (`int`), not as a `double`.

## Alternatives considered

**`decimal`.** Correct arithmetic, and the usual answer for financial software.
Rejected: 128 bits per value, slower, awkward across the network and in JSON, and it
still permits fractional values that the game has no use for. Integer minor units get
the same correctness more cheaply.

**Mutable balance column, no ledger.** Rejected. Simpler and faster, but it makes dupe
bugs permanently invisible and unfixable — the one failure this game cannot survive.

**`double`, with rounding at display time.** Rejected outright. This is the specific
mistake this ADR exists to prevent.

## Notes

Choose the minor-unit factor once, now, and never revisit it: **100 minor units = 1
credit**. Displayed prices are whole credits in nearly all UI; the minor units exist so
that percentage fees on small transactions do not round to zero.
