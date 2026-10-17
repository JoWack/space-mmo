# ADR-0016 — Death costs the materials you carry; gear and ships are kept, and wear

**Status:** Accepted · 2026-10-16 · Joe's direction of 15 October and his answers of 16 October ·
supersedes [ADR-0006](0006-death-and-insurance.md)

## Context

ADR-0006 made death the economy's material sink: everything a dead player owned resolved to survived,
damaged or destroyed by the cause of death, and ships were insured against their recorded cost. Nothing
could die while it was written, and nothing can yet.

On 15 October Joe opened it again: *"I'm also starting to re-think gear loss on death- maybe we restrict
that to something like only materials are lost, but weapons, ships, etc are kept."* Task 103 wrote down
what that would touch before anything was decided:

- **Insurance would have nothing to insure**, since ADR-0006 insures hulls only.
- **PvP keeps a stake through cargo**: hauling stays risky and raiding stays worth it (ADR-0008).
- **Demand for crafted gear.** EconSim measured loss as the standing source of demand. If weapons and
  ships are never lost, each player needs one of each and the crafters' market dries up, unless something
  else consumes gear: wear and repair, upgrades, or new tiers.

ADR-0017, decided the same day, answers the third in part: a weapon's damage comes from its crafted tier,
so better weapons are always worth making. This ADR answers the rest.

## Decision

Decided by Joe on 16 October, each offered against the alternatives below, Claude's suggestion first.

1. **Only materials are lost.** Every stackable good you carry, and in your ship's hold if the ship went
   down with you, is lost. What you wear, your weapons and tools, and your ship are kept. "Materials" means
   the stackable categories (`ItemCategory.IsStackable`): raw, refined, components and consumables.
2. **Half falls, half is destroyed.** Of each stack lost, half drops in a container where you fell that
   anyone may loot for 10 minutes, after which it is destroyed; the other half is destroyed at once. The
   destroyed half is the sink; the dropped half is what a raid wins, and what a run back recovers.
3. **What is kept wears.** Everything worn, and the ship if it went down, loses 10% condition. At 0 an item
   is broken: kept, unusable until repaired.
4. **Repairs never quite undo it.** Repairing restores an item's condition to its maximum and lowers that
   maximum a little, so gear eventually wears out and has to be replaced. In Joe's preview, 2 points a
   repair: a rifle that dies and is repaired sixty-odd times is worn out.
5. **Repair is a station service anyone can use**, at any station's industry bay, for materials (some of
   what the item is made of) and credits. A repair skill may come later; nothing here needs one.
6. **You come back at the station you last docked at.** A ship that went down is put away in that
   station's hangar, damaged, as a ship docked there is (task 153).
7. **Dying at a station costs nothing**, as in ADR-0006: hubs stay safe, or they become gank-farms.
8. **Insurance is retired.** With ships kept, it has nothing to insure. `acquisition_value` stays recorded on
   every instance: it is the cost basis markets and transfers already carry, and costs nothing to keep.

## Consequences

Positive:

- **Losing a fight costs what you were carrying, not the gear you built.** Trying combat, and PvE when it
  exists, is cheap enough to do; dying while hauling is what hurts, which is where ADR-0008 wants the risk.
- **Raiding still pays and hauling is still risky**, through the dropped half.
- **Gear has a lifetime.** Wear and the falling maximum replace loss as the steady demand for crafted
  weapons, armour and ships, alongside the tiers of ADR-0017. The wear per death and the maximum lost per
  repair are the new calibration surface, as ADR-0006's table was.
- **Repairs are a sink of both materials and credits**, and every player meets them.
- **No insurance means no insurance exploit.** ADR-0006 spent most of its length closing the fraud vector
  and accepting adverse selection; neither exists now.

Negative, and accepted:

- **A weaker sink than ADR-0006**, which destroyed 80–95% of a ship's contents when it exploded. EconSim's
  material sink gap (economy-design §5a–5b) has to be measured again with the new rule before its tuning
  means anything.
- **Disabling a ship rather than destroying it matters less.** ADR-0006 made "disable, don't destroy" the
  pirate's best play because disabling preserved cargo; now the ship is kept and the cargo half-lost either
  way. What a disabled ship that is boarded or not finished off gives up is undecided (below).
- **Anyone may loot the dropped container**, including a third party in faction space where attacking the
  dead player was forbidden (ADR-0008). Recorded as Joe chose it; whether faction space should keep a
  defended player's drop for them and their attacker is a question for when looting is built.
- **A corpse run recovers half.** A solo death far from anyone loses only the destroyed half to whoever runs
  back within 10 minutes, which is softer than ADR-0006. Respawning at the last docked station, possibly far
  away, is what limits it.
- **ADR-0006's code is now unused.** The death resolver and rule table in `SpaceMMO.Domain.Loss` were built
  to its cause table (whose data file, `data/death-rules.json`, was never authored); task 103 replaces them
  rather than calling them.
- **A broken item in a slot** stays equipped and unusable until repaired: the tool gates already refuse a
  laser at condition 0, so a player can die with their mining laser and find mining refused until they repair it.

## Alternatives considered

**Keep ADR-0006**: cause-based survived, damaged or destroyed for everything, insured hulls. Rejected by Joe
in favour of his 15 October direction.

**Materials and ships lost, gear kept**: space stays as dangerous as ADR-0006 made it while dying on foot
becomes cheap. Not chosen.

**Wear that repairs fully undo**: gear lasts for ever, and crafters sell only upgrades and new tiers. Not
chosen; the falling maximum is what keeps gear a consumable.

**Nothing happens to kept gear**: the only sink left would be the materials lost. Not chosen.

**All dropped**: raiding pays fully, but death is only a transfer, never a sink, and a solo death is fully
recovered by running back. **All destroyed**: the strongest sink, but a raid pays nothing except denial,
and piracy has no point. Neither chosen.

**Respawn at the home world's capital**: always the same place, but a long trip back from anywhere else, and
the ship lands at home. **At the nearest station you may use**: closest to the loot, but "may use" across
factions (ADR-0008) needs defining. Neither chosen.

**Repair through the crafting skill that made the item**, as a job earning that skill's XP: a fighter without
the skill must find a crafter, and nothing yet lets one player hand an item to another outside the market.
**A station, cheaper with the skill**. Neither chosen; a repair skill can still come later.

## Open

- What a disabled ship gives up when it is not destroyed: whether its hold can be looted, and by whom.
- Whether faction space keeps a defended player's drop from third parties.
- The materials and credits a repair costs, per item; the wear per death and maximum lost per repair are
  Joe's preview numbers (10% and 2) until EconSim says otherwise.
