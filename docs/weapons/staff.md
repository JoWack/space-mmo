# Staff — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided** beyond the family's
charge and Dissonance ([README](README.md)). Task 192, [ADR-0015](../adr/0015-every-weapon-is-a-skill.md).
Numbers are first guesses; none has been tested.

**Resonance family, ranged, on foot.** Builds 6 charge a hit. Shares 20% of its XP with the Focus, so a
healer levels it slowly alongside.

## The idea

**The caster's weapon.** Slower projectiles than any gun, but they fill an area, bend around a target or
hold it in place. Hits build charge, and charge becomes waves: the Staff gets stronger as a fight goes on,
then spends it all at the moment that matters. Its trees are **Wavecaster** (area damage) and **Gravity**
(moving people about).

## Attacks

| Attack | Input | What it does | Charge |
|---|---|---|---|
| **Resonant bolt** | Click | A slow, glowing bolt to 30 m. Applies one stack of Dissonance. | +6 on a hit |
| **Wave** | Hold, release | Spends your charge on a wave that rolls out 12 m ahead, 4 m wide, hitting everyone in it. Damage scales with the charge spent: at 100, about six bolts' worth. | Spends all |

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Wavecaster — fill the space

| Node | Kind | Effect |
|---|---|---|
| **Shockwave** | Active, 14 s | A ring bursts outward from you to 6 m, damaging and pushing enemies back 3 m. Costs 30 charge. |
| ↳ Wider ring | Upgrade | 9 m. |
| ↳ Echoing | Upgrade | A second, weaker ring follows a second later. |
| **Standing wave** | Active, 18 s | Plant a pulsing field 5 m across for 6 s: it hits everyone inside once a second, each pulse adding Dissonance. |
| ↳ Resonant field | Upgrade | You gain 4 charge for every enemy pulsed. |
| ↳ Hold the field | Upgrade | It lasts 3 s longer. |
| **Crescendo** | Active, 22 s | Your next Wave is free to spend and counts as full charge. |
| ↳ Encore | Upgrade | It leaves a Standing wave where it ends. |
| ↳ Fortissimo | Upgrade | That Wave is twice as wide. |
| Harmonics | Passive | Bolts that hit a target with Dissonance build 2 more charge. |
| Long wave | Passive | Waves roll 18 m. |
| Overtone | Passive | Hits on a target with three Dissonance stacks deal +15%. |
| Weak point | Passive | Hits to a weak point critically hit, +25%. |
| **Resonance cascade** | Capstone | A full-charge Wave refunds half its charge for every enemy beyond the first that it hits. |

### Gravity — hold them where you want them

| Node | Kind | Effect |
|---|---|---|
| **Well** | Active, 16 s | A gravity well 6 m across for 3 s: enemies inside are dragged toward its centre and slowed. Costs 25 charge. |
| ↳ Deeper | Upgrade | The pull is twice as strong. |
| ↳ Collapse | Upgrade | It ends in a burst that damages everyone still inside. |
| **Lift** | Active, 20 s | Raises one target off the ground for 2 s: they cannot move, dodge or attack. |
| ↳ Drop | Upgrade | They land hard: damage and a stack of Dissonance. |
| ↳ Suspended | Upgrade | 3 s. |
| **Repel** | Active, 12 s | A burst of force in a cone that throws enemies back 6 m. |
| ↳ Into the wall | Upgrade | Anyone thrown into a wall is staggered. |
| ↳ Clear the way | Upgrade | Allies in the cone are untouched and move 20% faster for 2 s. |
| Heavy air | Passive | Your bolts slow their target 10% for 2 s. |
| Mass | Passive | Gravity effects last 25% longer. |
| Pull through | Passive | Enemies moved by your abilities build you 5 charge each. |
| Grounded | Passive | You cannot be knocked back or lifted while channelling a Wave. |
| **Event horizon** | Capstone | A Well cast with 80 charge or more pulls everyone inside to its centre at once and holds them there for its last second. |

## What the draft is trying to make possible

- **A Wavecaster** behind a front line: Standing wave to build charge, Crescendo into a full Wave.
- **A Gravity controller** for a group: Well or Lift a target for the group's burst, Repel to protect the
  healer.
- **With the Focus**: the healer's second weapon, levelling alongside it.

## Questions this draft raises

- **Decided 16 October:** Lift and Well fall under **diminishing returns** in PvP
  ([README](README.md#combat-rules)). Into the wall now staggers rather than inventing a "dazed" status
  beside Scorch, Stagger and Dissonance (Claude's tidy, flagged in the draft).
