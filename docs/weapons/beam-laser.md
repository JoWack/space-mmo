# Beam Laser — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided** beyond the ship
weapon rules and the Laser family's heat ([README](README.md#ship-weapons)). Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses; none has been tested.

**Laser family, ship weapon.** Gimballed within about 15°. Heat: +6 a pulse, +20 a second while beaming,
draining at 25 a second after a second; overheated for 3 s, then resumes at 50. No ship sibling yet, so it
keeps all of its XP.

## The idea

**The ship's precise weapon**: light-speed, so what you aim at is what you hit, at long range, with heat
the only limit. A pulse for chipping, a held beam for the kill. Its trees are **Lance** (range and
precision, a ship's weak points) and **Burn** (sustained beams, Scorch on the hull, overloading systems).

## Attacks

| Attack | Input | What it does | Heat |
|---|---|---|---|
| **Pulse** | Click | A short pulse, hitting instantly to 4 km. | +6 |
| **Beam** | Hold | A continuous beam to 3 km for as long as it is held, dealing more damage the longer it stays on one ship, to double after 2 s. | +20 a second |

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Lance — reach and precision

| Node | Kind | Effect |
|---|---|---|
| **Focused lance** | Active, 14 s | One pulse charged for 1 s: four times the damage, to 6 km. |
| ↳ Overpenetrate | Upgrade | It passes through the first ship to a second behind. |
| ↳ Quick focus | Upgrade | Charges in 0.6 s. |
| **Target lock** | Active, 20 s | Locks the gimbal onto one ship for 6 s: it stays on target anywhere in a 30° cone. |
| ↳ Shared lock | Upgrade | Your wingmates' gimbals widen to 25° against that ship. |
| ↳ Steady lock | Upgrade | 9 s. |
| **Subsystem strike** | Active, 16 s | Your next three pulses hit the target ship's systems rather than its hull: engines first, then weapons. |
| ↳ Engines down | Upgrade | A ship whose engines are hit this way is slowed 30% for 4 s. |
| ↳ Guns down | Upgrade | Its weapons build 50% more heat or lose a round per shot for 4 s. |
| Long focus | Passive | +10% damage beyond 2 km. |
| Weak point | Passive | Hits to a ship's weak spots (engines, hardpoints) critically hit, +30%. |
| Cool optics | Passive | Pulses build 25% less heat. |
| Wide gimbal | Passive | The gimbal swivels 20°. |
| **Pinpoint lance** | Capstone | A Focused lance on a weak spot disables that system for 3 s. |

### Burn — keep it on them

| Node | Kind | Effect |
|---|---|---|
| **Sweep** | Active, 18 s | A 2 s beam that keeps hitting everything the gimbal crosses, scorching each. |
| ↳ Afterglow | Upgrade | Scorch from Sweep lasts twice as long. |
| ↳ Wide sweep | Upgrade | The gimbal swings 30° while sweeping. |
| **Overload** | Active, 20 s | A beam that, held on one ship for 2 s, overloads its systems: its heat or its rounds are pushed to the limit. |
| ↳ Cascade | Upgrade | An overloaded ship's systems are 20% more vulnerable for 4 s. |
| ↳ Quick overload | Upgrade | 1.5 s. |
| **Heat dump** | Active, 16 s | Vent all your heat backward as a plume: ships behind you within 1 km are scorched and their sensors blinded for 2 s. |
| ↳ Thick plume | Upgrade | The plume hangs for 4 s, scorching anything that flies through. |
| ↳ Clean vent | Upgrade | Dumping heat also refunds 3 s of Sweep's cooldown. |
| Scorch | Passive | Pulses scorch the hull. |
| Running hot | Passive | Damage rises with heat, to +20% at 80 heat. |
| Radiators | Passive | Heat drains 25% faster. |
| Persistent | Passive | The Beam reaches double damage in 1.5 s. |
| **Burnthrough** | Capstone | A Beam held on one ship for 4 s ignores its armour and Scorch stacks to five. |

## Questions this draft raises

- **Decided 16 October:** ships have **systems**, engines, weapons and the hold, each with its own health
  ([README](README.md#ship-weapons)), which Subsystem strike and Overload hit.
- **Blinded sensors** (Heat dump) is blindness again, on a cockpit this time.
- **Four kilometres** sounds long and is about five seconds' flight at top speed (ADR-0001's 1:10 scale):
  ranges want measuring against the flight model before any is trusted.
