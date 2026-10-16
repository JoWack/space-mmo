# Railgun — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided** beyond the ship
weapon rules and the Kinetic family's rounds and Stagger ([README](README.md#ship-weapons)). Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses; none has been tested.

**Kinetic family, ship weapon.** Gimballed within about 15°. Eight slugs a magazine, a 4 s reload, built
into the weapon like every Kinetic's rounds. No ship sibling yet, so it keeps all of its XP.

## The idea

**Heavy, fast slugs that knock ships about.** Not instant like the Beam Laser: a slug takes time to arrive,
so you lead your target, and when it lands it **jolts** the ship off its line (a ship's Stagger). Its trees
are **Disabler** (stopping a ship without destroying it, ADR-0006's way to take cargo) and **Point
defence** (shooting down missiles, and flak against small fast things).

## Attacks

| Attack | Input | What it does | Rounds |
|---|---|---|---|
| **Slug** | Click | A slug at 6 km a second to 5 km. Adds 30 stagger. | 1 |
| **Charged shot** | Hold, release | Charges for up to 1.5 s: at full, twice the damage, 80 stagger, and it passes through the first ship. | 2 |

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Disabler — stop them, keep the cargo

| Node | Kind | Effect |
|---|---|---|
| **Engine shot** | Active, 16 s | A slug aimed at the engines: on a hit, the ship's thrust is halved for 5 s. |
| ↳ Dead stick | Upgrade | Its turning is halved too. |
| ↳ Lasting | Upgrade | 8 s. |
| **Ion slug** | Active, 20 s | A slug that does almost no hull damage, but shuts down the system it hits for 3 s. |
| ↳ Spread ion | Upgrade | It shuts down every system within reach of the impact. |
| ↳ Deep ion | Upgrade | 5 s. |
| **Grapple slug** | Active, 24 s | A tethered slug: a ship it hits is held within 1 km of you for 6 s. |
| ↳ Reel in | Upgrade | It is pulled to 300 m. |
| ↳ Strong line | Upgrade | 9 s. |
| Heavy slugs | Passive | Slugs add 25% more stagger. |
| Weak point | Passive | Hits to engines and hardpoints critically hit, +30%. |
| Gentle hands | Passive | You deal 30% less damage to a disabled ship's hull, so a fight does not end in an explosion you did not want. |
| Lead the shot | Passive | A marker shows where to aim to hit a moving target. |
| **Dead in space** | Capstone | A ship whose engines and weapons are both shut down at once is **disabled** for 8 s: it drifts, and its hold can be opened. |

### Point defence — nothing gets through

| Node | Kind | Effect |
|---|---|---|
| **Flak burst** | Active, 12 s | A slug that bursts 1 km ahead into a cloud of fragments: missiles in it are destroyed, small ships damaged and staggered. |
| ↳ Dense flak | Upgrade | The cloud is twice as wide. |
| ↳ Lingering | Upgrade | It lingers for 2 s. |
| **Intercept** | Active, 18 s | For 4 s the gimbal fires on its own at any missile within 2 km heading for you or a wingmate. |
| ↳ Wide watch | Upgrade | 3 km. |
| ↳ Spare rounds | Upgrade | Intercepting costs no rounds. |
| **Screen** | Active, 22 s | Fires a wall of slugs to one side: for 3 s, missiles and slugs crossing it are stopped. |
| ↳ Wide screen | Upgrade | Twice as long. |
| ↳ Return fire | Upgrade | Whatever the screen stops staggers the ship that fired it. |
| Fast loader | Passive | Reloads take 25% less time. |
| Deep magazine | Passive | Twelve slugs. |
| Tracking | Passive | Slugs fired at missiles home in on them slightly. |
| Escort | Passive | Wingmates within 2 km take 10% less damage from missiles. |
| **Iron curtain** | Capstone | Each missile you destroy refunds a round and 2 s of Intercept's cooldown. |

## Questions this draft raises

- **Opening the hold** (Dead in space) is ADR-0006's disabling made concrete: a disabled ship's cargo can be
  taken. It assumes looting a ship is a thing someone does, which the death rethink (task 103) may change.
- **Grapple slug** holds a ship in place, which in PvP may feel worse than being destroyed.
- **Decided 16 October:** ships have **systems** (engines, weapons, the hold), so Ion slug and Dead in
  space have something to hit; missiles are countered by a module **and** Point defence; Grapple slug stays,
  under diminishing returns ([README](README.md#ship-weapons)).
