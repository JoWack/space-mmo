# Assault Rifle — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided**, including the
Kinetic family's rounds and Stagger, which are proposals ([README](README.md)). Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses; none has been tested.

**Kinetic family, ranged, on foot.** 30 rounds a magazine, a 2 s reload. Shares 20% of its XP with the
Magnum.

## The idea

**The front line's gun.** Middle range, automatic, and better the longer a fight lasts. Where the Laser
Rifle picks shots, the Assault Rifle puts a lot of metal in the air: it staggers, pins people in place and
breaks up groups. The magazine is its rhythm: thirty rounds of pressure, then a reload you should choose
rather than be caught by. Its trees are **Assault** (pressure and explosives) and **Specialist**
(choosing what goes in the magazine).

## Attacks

| Attack | Input | What it does | Rounds |
|---|---|---|---|
| **Auto fire** | Click and hold | Fully automatic. Accurate for the first few rounds, climbing as you keep firing. Each hit adds 8 stagger. | 1 a shot |
| **Aimed burst** | Hold the heavy input | Steadies the rifle: you move at half speed, and each click fires a tight three-round burst that adds 30 stagger if all three land. | 3 a burst |

Reloading early keeps the rounds left; reloading empty is half a second slower.

## The trees

14 nodes each, 28 in all, for 20 points: a capstone needs 10 points in its tree; an active costs 1 and each
of its two upgrades 1 more.

### Assault — keep them pinned

| Node | Kind | Effect |
|---|---|---|
| **Underbarrel grenade** | Active, 16 s | Lobs a grenade that bursts 3 m wide: damage, and 50 stagger to everyone caught. |
| ↳ Cluster | Upgrade | Splits into three smaller blasts on landing. |
| ↳ Concussive | Upgrade | Knocks everyone it hits back 3 m. |
| **Suppressing fire** | Active, 18 s | For 4 s, every round you fire into an area 6 m wide slows enemies there 25% and spoils their aim. |
| ↳ Pinned | Upgrade | Enemies who stay in the area for 2 s are staggered. |
| ↳ Drum | Upgrade | Rounds fired during it are free. |
| **Charge** | Active, 14 s | Sprint 10 m, shoulder-checking the first enemy in your way for 60 stagger. |
| ↳ Point blank | Upgrade | Your next 10 rounds after the charge deal +20%. |
| ↳ Bowl over | Upgrade | Enemies within 2 m of the one you hit are knocked aside. |
| Heavy hand | Passive | Hits add 25% more stagger. |
| Steady climb | Passive | Recoil climbs half as fast. |
| Extended magazine | Passive | 40 rounds. |
| Weak point | Passive | Hits to a weak point critically hit, +25%. |
| **Overwhelm** | Capstone | Staggered enemies take 30% more damage from you for 3 s after the stagger ends. |

### Specialist — what goes in the magazine

Three loads, one at a time: choosing an active **reloads** the rifle with that magazine. Each lasts until it
is spent or swapped.

| Node | Kind | Effect |
|---|---|---|
| **Piercing rounds** | Active, 20 s | A magazine of rounds that ignore 30% of armour and pass through the first target. |
| ↳ Tungsten | Upgrade | Ignore 45% of armour. |
| ↳ Shattering | Upgrade | Piercing hits add double stagger to armoured targets. |
| **Tracer rounds** | Active, 20 s | A magazine whose hits **expose** the target to allies for 3 s, and show where you are shooting. |
| ↳ Spotter | Upgrade | Exposed targets take 10% more damage from allies. |
| ↳ Paint | Upgrade | Tracer hits mark weak points on creatures for the whole group. |
| **Resupply** | Active, 30 s | Throw a box that refills every ally's magazine within 5 m, and your own. |
| ↳ Spare mags | Upgrade | The box stays 10 s, refilling anyone who reaches it. |
| ↳ Field kit | Upgrade | Allies who take from it reload 30% faster for 8 s. |
| Quick hands | Passive | Reloads take 30% less time. |
| Loadout | Passive | Swapping loads keeps the rounds left in the old magazine for later. |
| Measured | Passive | The first 5 rounds of a magazine deal +15%. |
| Ammo belt | Passive | Rounds are refunded 1 in 5 on a weak-point hit. |
| **Full kit** | Capstone | Each load you have taken grants its magazine +5 rounds, and switching loads is instant. |

## What the draft is trying to make possible

- **An Assault front-liner** with the Magnum or the Daggers: Charge in, Suppressing fire, Overwhelm the
  staggered.
- **A Specialist support** who never needs to aim well: Tracer rounds and Resupply make the group better.
- **With the Focus**: the Assault Rifle is a good partner for the healer's second weapon.

## Questions this draft raises

- **Rounds as an item** ([README](README.md)): if magazines draw on ammunition, Resupply is a way for one
  player to spend their own stock on everyone's.
- **Exposed** again (Tracer rounds), and the PvP care it needs.
- **Armour** (Piercing rounds) has no stats yet; task 179.
