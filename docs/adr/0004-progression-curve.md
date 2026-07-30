# ADR-0004 — Adopt the RuneScape XP curve unmodified

**Status:** Accepted · 2026-07-29

## Context

The design calls for RuneScape-style skills: levels 1–99, deliberately grindy, with
items and capabilities unlocking at thresholds. A progression curve therefore has to
be chosen, and it is effectively permanent — once players have invested hundreds of
hours, the curve cannot be changed without either invalidating that time or handing
out free levels.

The curve has to satisfy several properties simultaneously: early levels fast enough
to feel responsive, late levels slow enough that 99 is a genuine achievement, and a
smooth ratio between consecutive levels so no single level feels like a wall.

## Decision

**Use the RuneScape XP curve exactly as-is.**

```
xp(1) = 0
xp(L) = floor( Σ(n=1..L-1) floor( n + 300 · 2^(n/7) ) / 4 )
```

Key values, which serve as the unit tests:

| Level | Total XP |
|---|---|
| 1 | 0 |
| 2 | 83 |
| 10 | 1,154 |
| 50 | 101,333 |
| 92 | 6,517,253 |
| 99 | **13,034,431** |

XP is stored as `long` per `(character, skill)` pair; **level is always derived, never
stored**. The curve is implemented as a precomputed lookup table with a binary search
for the reverse mapping.

## Consequences

Positive:

- Twenty-plus years of live-service evidence that this curve is tolerable to grind and
  motivating to complete. That evidence cannot be bought any other way.
- The near-geometric shape means level 92 is the halfway point to 99 in XP terms — a
  well-known property that gives late progression a satisfying, legible structure.
- Deriving level from XP makes it impossible for level and XP to disagree, removing an
  entire class of desync bug.
- Players familiar with RuneScape arrive already understanding the pace.

Negative, and accepted:

- The curve is unmistakably borrowed. It is a mathematical formula rather than
  protected expression, and the values are widely published, but the resemblance is
  the point rather than something to disguise.
- Extremely grindy by modern standards. This is intentional per the design brief, and
  it will not suit every player.
- Content must span the entire curve. A curve implying thousands of hours per skill
  needs enough activities to fill them, which is a substantial content obligation.

## Alternatives considered

**A custom curve.** Rejected: no way to validate it without years of live data, and
the failure mode (discovering at level 70 that the curve is wrong) is unrecoverable.

**A linear or gentler curve.** Rejected: contradicts the explicit design goal of a
grindy, long-horizon progression system.

**EVE's time-based skill training.** Genuinely tempting, since it suits a
sandbox economy and respects players' time. Rejected because it contradicts the stated
RuneScape-style design and, more importantly, because it removes the link between
*doing* an activity and getting better at it — a link the crafting economy depends on.

## Notes

Implemented in `SpaceMMO.Domain/Progression/SkillCurve.cs` as a pure static class with
no dependencies, so it is trivially testable and usable from EconSim without a
database.
