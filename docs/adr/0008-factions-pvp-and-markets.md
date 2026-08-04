# ADR-0008 — Faction warfare, PvP zoning, and market structure

**Status:** Accepted · 2026-08-03 · resolves open question §1 of the design bible ·
depends on [ADR-0007](0007-one-handcrafted-system.md)

## Context

The design bible assigned four races to two factions and then said, deliberately, that
"faction implications are unscoped for now," because deciding warfare rules before the
economy was balanced would have constrained it. The economy is now built, tested, and
simulated. This decides the rules that were deferred.

The shape of the system — a capital at the centre with four homeworlds around it —
makes two things possible that a single undifferentiated space does not: materials
that exist in only one place, and space that belongs to somebody.

## Decision

### Factions

Unchanged from design bible §1, now with consequences attached:

| Faction | Races | Homeworlds |
|---|---|---|
| `faction_a` | Humanoid, Martian | Terra, Ares |
| `faction_b` | Space Elf, Space Orc | Verdance, Grimhold |

Members of a faction are friendly to each other and hostile to the other faction.

### Space is divided by a plane

The system is split down the middle. One half is `faction_a` space, the other is
`faction_b` space, and the capital sits on the line inside a neutral sphere.

**PvP legality is therefore a pure function** of attacker faction, target faction, and
a position — no regions table, no zone actors, no new state. It belongs in
`SpaceMMO.Domain` with the rest of the rules, testable with zero I/O:

```
CanAttack(attacker, target, positionInSystem) -> bool
```

The capital's neutral sphere is expressed the same way. `security_level` already
exists on bodies and does not need extending for this.

### Materials are planet-locked; markets are stratified by geography

- **Raw materials are distinct `item_def`s per planet.** Grimhold slag exists on
  Grimhold and nowhere else. This is what makes hauling a profession and gives the
  PvP line something to be a line *between*.

- **Crafted goods are one `item_def` with a style tag**, produced by four per-race
  recipes that consume different raw inputs and yield the same output. Four races'
  worth of plate share one order book, so the market stays liquid at small population
  while style stays visually real.

- **The capital hosts a global market**: any item, any race, any faction, one book.

- **Homeworld stations host local markets.** They are *not* restricted by rule to
  their own race's goods. They do not need to be: raw materials only occur on one
  planet, so the sellers and the natural buyers are already standing on the same
  ground. Geography does the work a restriction would have done, without forbidding a
  hauler from making a legitimate arbitrage trade.

  The capital's higher market fee and the flight time between bodies are the levers
  that keep local books alive. Both are tunable numbers in content, not code.

### High-tier recipes cross faction lines

Early and mid-tier chains — everything in the onboarding questline — stay
**self-sufficient within a race**, so a new player never stalls waiting for a market
that may have no sellers. High-tier ships and equipment require materials from all
four planets.

This is the load-bearing decision of the whole document. Without it, every race mines
its own ore, crafts its own goods, and never needs anyone: the global market has
nothing to trade *across*, and the PvP split is scenery.

## Consequences

Positive:

- Hauling, raiding, blockade-running, and price differentials between the four locals
  and the capital all fall out of two facts (materials are planet-locked, high tiers
  need all four) rather than from bespoke systems.
- PvP has an economic stake instead of being voluntary duelling.
- The capital earns its existence: it is the only place a `faction_a` player can buy
  `faction_b` materials without shooting someone.

Negative, and accepted:

- **The capital is on the border, so the route from any homeworld to the capital stays
  inside friendly space.** Hauling to the global market is therefore risk-free by
  default. The stakes come entirely from *needing* foreign materials — which means if
  cross-faction demand is ever tuned out of the recipe graph, the PvP zone silently
  becomes decoration. This is the invariant to watch, and EconSim should assert it.
- **A style tag changes the inventory stack key.** `inventory_items` is currently keyed
  by `(inventory_id, item_def_id)`; it becomes `(inventory_id, item_def_id, style)`.
  That is a migration, and it touches `InventoryService` stacking and cost-basis
  tracking. Cheap now, expensive after players own things.
- Four raw-material sets and four per-race recipe variants multiply the content to
  author and to balance. EconSim will need all four modelled before the numbers can be
  trusted.
- PvP means player-versus-player loss, which puts weight on
  [ADR-0006](0006-death-and-insurance.md) far earlier than planned.

## Alternatives considered

**Local markets restricted by rule to their own race's goods.** Rejected. The
restriction is nearly redundant once materials are planet-locked, and it forbids
legitimate arbitrage — a hauler who buys Grimhold slag at the capital could not resell
it on Verdance. A rule that mostly duplicates geography while creating a dead end is
not worth its failure modes.

**Distinct `item_def`s for every race variant of every crafted good.** Rejected on
liquidity: it quarters the depth of every order book. At solo-project population, a
quarter-depth book frequently means no counterparty at all, and a market with no
counterparty teaches players that the economy does not work.

**Everything shared, style purely cosmetic.** Rejected: race identity stops mattering
economically, which removes the reason for four distinct planets to exist.

**Cross-faction requirements from the first recipe.** Rejected: it makes a brand-new
character dependent on a market that may be empty, which is the worst possible first
hour.
