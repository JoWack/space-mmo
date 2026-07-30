# ADR-0006 — Cause-based loot destruction, and insurance pegged to acquisition value

**Status:** Accepted · 2026-07-29

## Context

Death mechanics set the material sink, and the material sink is what gives industry a
reason to exist. If crafted goods accumulate forever, every price falls permanently
toward raw-material cost and manufacturing stops being a viable profession. So how
much a death destroys is not a combat-design question — it is *the* economic lever.

Two things had to be decided together, because they interact:

1. **What survives a death.** A binary full-loss rule is simple but crude: it makes a
   ship exploding and a player being shot on a hillside identical events, which is
   neither believable nor interesting.
2. **Whether losses can be hedged.** Insurance softens loss aversion and lets players
   take fights they would otherwise decline — but insurance is a credit faucet, and a
   badly specified one is an unbounded credit printer. EVE shipped exactly that bug and
   spent years rebalancing around it.

## Decision

### Cause-based, three-outcome loot resolution

Every item resolves to **survived**, **damaged**, or **destroyed**, with probabilities
drawn from a `(death cause × item category)` table in `data/death-rules.json`.

Six causes: `ShipExplosion`, `ShipDisabled`, `PersonalCombatPlanet`,
`PersonalCombatStation`, `EnvironmentalPlanet`, `SelfDestruct`. Full table in
[economy-design.md](../economy-design.md) §3b.

The load-bearing rows:

- **Explosion destroys 80–95% of everything.** Your cargo was inside the thing that
  detonated.
- **Disabling preserves most cargo.** A deliberate tactical fork: a pirate who wants
  loot must disable rather than obliterate, which is materially harder.
- **Armor never survives on-foot planetary combat intact** — 70% damaged, 30%
  destroyed. It was the thing being shot.
- **Station deaths lose nothing.** Hubs must be genuinely safe or they become
  gank-farms.

Resolution is a **pure function of an explicit seed**, drawn by the server and recorded
on the death record.

### Insurance pegged to recorded acquisition value

Optional, four tiers (none / 40% / 60% / 80% payout for 0 / 6 / 12 / 20% premium),
covering the hull only. Four rules make deliberate loss unprofitable:

1. Payout is pegged to `acquisition_value` **recorded on the ship instance at
   creation** — actual input material value if crafted, actual price paid if bought.
   Never a market reference price or rolling average.
2. Payout rate is **strictly below 100%**, so net result on any loss is
   `r·V − V − P = −V(1−r) − P < 0`.
3. **Self-destruct voids the policy.**
4. **Premiums are non-refundable**, paid up front.

Rule 1 is the one that actually matters. The fraud vector exists only when payout is
pegged to a reference price that an efficient industrialist can beat: if payout is
`r × V_reference` and a player can build for `V_actual < V_reference`, then
`r × V_reference > V_actual` becomes achievable and destroying your own ships turns a
profit. Pegging to what the ship actually cost removes the gap the exploit lives in.

## Consequences

Positive:

- The destruction table is a single, data-driven calibration surface for the whole
  material economy. Tuning the sink is a JSON edit, not a code change.
- Believability comes free, and it points play in interesting directions — "disable,
  don't destroy" is emergent from the numbers rather than bolted on as a rule.
- Insurance lets risk-averse players engage with combat, without any arrangement of the
  rules making deliberate destruction profitable.
- Seeded resolution means the resolver is trivially unit-testable, EconSim can
  reproduce destruction runs exactly, and player disputes over a lost item are
  replayable from the log rather than a matter of opinion.

Negative, and accepted:

- **Insurance cannot be made a net sink.** Players choose when to insure, so they will
  insure ships they expect to lose — textbook adverse selection, with no underwriting
  available to defend against it. Insurance will run as a net faucet among combat
  players. Accepted because it is **bounded by production**: nobody can destroy more
  ships than they build, so it can never mint more than `payout_rate` of the material
  value actually consumed. A faucet tied to genuine economic activity is the only kind
  of unbounded-looking faucet that is safe.
- **"Damaged" requires an item condition system**, which requires per-instance item
  rows for everything non-stackable, which is a real schema and UI cost. It also implies
  a repair loop that is deliberately not designed yet, leaving `condition` partly inert
  until then.
- **Nine categories × six causes is 54 tuned numbers.** That is a lot of surface to
  balance, and most of it cannot be validated without live combat data.
- Recording `acquisition_value` per hull means every creation path — craft, buy,
  quest reward, future gifting or salvage — must set it correctly. **A path that
  forgets to is an exploit**, so this needs a non-nullable column and a test per
  creation path.
- Cause classification becomes gameplay-visible and therefore contestable. Players will
  argue about whether a given kill was an explosion or a disable, so the distinction has
  to be legible in-game, not just in the simulation.

## Alternatives considered

**Full loss on every death, EVE-style.** Rejected: strongest possible sink and dead
simple, but it makes all deaths identical and forecloses the disable-for-loot dynamic,
which is the most interesting thing this design produces.

**No loss; repair costs only.** Rejected: too weak a material sink. Goods accumulate,
prices collapse to material cost, and industry dies.

**Insurance payout pegged to current market value.** Rejected — this *is* the exploit.
It is also the intuitive design, which is exactly why it's worth recording as rejected.

**A funded insurance pool that can run dry.** Genuinely appealing, because premiums
funding payouts makes insurance a pure transfer with zero faucet contribution.
Rejected on player-experience grounds: a payout that fails because other players
haven't bought enough insurance is indefensible at the moment a player loses a ship.
The pool is tracked as an EconSim *metric* instead, so bad rates surface in testing
rather than at the worst possible moment in-game.

**Fixed insurance prices per hull type rather than per-instance value.** Rejected: this
is the reference-price formulation, one step from the exploit again.

## Notes

`acquisition_value` and `condition` go into the first migration even though the repair
loop is deferred. Adding columns to tables full of live player items is far worse than
carrying unused ones.
