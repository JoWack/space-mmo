# Laser Pistol — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided.** Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses; none has been tested.

**Laser family, ranged, on foot.** Uses the family's heat ([README](README.md)): +5 a shot, draining at 30
a second after half a second. Shares 20% of its XP with the Laser Rifle.

## The idea

The Rifle stands still and makes each shot count. **The Pistol moves.** Short to middle range, fast shots,
heat that builds quickly and clears quickly, and good at being the second weapon: you fire it while the
other cools, or swap to it when someone closes in. Its trees are the two ways to play that: **Gunslinger**
(movement and the swap) and **Overclock** (heat as speed).

## Attacks

| Attack | Input | What it does | Heat |
|---|---|---|---|
| **Snap shot** | Click | Quick single shots, accurate while moving. | +5 |
| **Burst** | Hold | Fires four shots in quick succession for as long as it is held, with a little spread. | +20 a burst |

Overheated for 1.5 s, then resumes at 40.

## The trees

14 nodes each, 28 in all, for 20 points, as the Rifle's: a capstone needs 10 points in its tree; an
active costs 1 and each of its two upgrades 1 more.

### Gunslinger — move, swap, shoot first

| Node | Kind | Effect |
|---|---|---|
| **Roll shot** | Active, 10 s | A dodge roll in any direction, firing a snap shot at whatever you aim at mid-roll. |
| ↳ Second roll | Upgrade | Can be used twice before going on cooldown. |
| ↳ Slip | Upgrade | You take 30% less damage for 1 s after rolling. |
| **Quickdraw** | Active, 14 s | For 3 s after you swap to the Pistol, your shots critically hit and add no heat. Can also be used without swapping, for 1.5 s. |
| ↳ Holster | Upgrade | Swapping away from the Pistol drains all its heat. |
| ↳ Steady hands | Upgrade | Quickdraw lasts 1 s longer. |
| **Ricochet** | Active, 12 s | A shot that bounces to up to two more enemies within 8 m, losing a third of its damage each bounce. |
| ↳ Bank shot | Upgrade | Bounces can find enemies behind cover. |
| ↳ Chain | Upgrade | Each target hit is scorched. |
| Run and gun | Passive | No accuracy lost while moving or jumping. |
| Fast hands | Passive | Swapping weapons takes half as long. |
| Momentum | Passive | +10% damage for 3 s after a dodge or a sprint. |
| Weak point | Passive | Hits to a weak point critically hit, +30%. |
| **Draw first** | Capstone | Your first hit on someone who has not hit you in the last 10 s deals +40%, and refreshes Roll shot. |

### Overclock — the hotter, the faster

| Node | Kind | Effect |
|---|---|---|
| **Overclock** | Active, 18 s | For 5 s the Pistol fires twice as fast and builds heat twice as fast. |
| ↳ Cooling loop | Upgrade | When it ends, drop to 30 heat. |
| ↳ Sustain | Upgrade | Lasts 2 s longer. |
| **Flare** | Active, 16 s | A shot that bursts into light: reveals hidden enemies within 10 m for 4 s and **exposes** the target's weak points to your allies. |
| ↳ Dazzle | Upgrade | Enemies facing the burst are **blinded** for 1 s. |
| ↳ Lingering | Upgrade | The light lasts 8 s. |
| **Heat lance** | Active, 12 s | Dumps all your heat into one point-blank shot: the more heat, the more damage, up to five times a snap shot. |
| ↳ Pierce | Upgrade | The lance passes through its first target. |
| ↳ Afterburn | Upgrade | The target is scorched three times. |
| Spin-up | Passive | Fire rate rises with heat, to +30% at 70 heat and above. |
| Heat bleed | Passive | Weak-point hits take 10 heat off. |
| Hot rounds | Passive | Snap shots scorch while above 50 heat. |
| Vent fins | Passive | Heat drains 25% faster. |
| **Redline** | Capstone | At 90 heat and above, shots pierce and the Pistol no longer overheats; instead each shot costs you 2% of your health. |

## What the draft is trying to make possible

- **A Rifle and Pistol pair**: Marksman Rifle at range, swapping to a Gunslinger Pistol (Quickdraw,
  Holster) when someone closes in. The two share XP, so playing both levels both.
- **A full Overclock Pistol as the main weapon**: Overclock into Heat lance, Redline for the brave.
- **Flare as a team tool**: exposing weak points for a group, from either build.

## Questions this draft raises

- **Blindness** (Dazzle) and **revealing hidden enemies** (Flare) need the designs the [README](README.md)
  lists. Nothing hides yet, so "hidden" assumes stealth or cover that does not exist.
- **Redline costs health**, which is the first thing in any draft that hurts its user. It might be too
  punishing alongside ADR-0006's loss on death.
- **Draw first** rewards whoever shoots first, and ADR-0009 already decides who may shoot first in faction
  space. Probably fine, since the rule says who may and this says who gains; worth a look.
