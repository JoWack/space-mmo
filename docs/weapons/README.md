# Weapons

The design of each weapon, per [ADR-0015](../adr/0015-every-weapon-is-a-skill.md) and task 192. Each weapon
has a page with its basic and heavy attacks and its two trees; this page holds what a whole family shares.

| Family | On foot, ranged | On foot, melee | Mechanic |
|---|---|---|---|
| Laser | [Laser Rifle](laser-rifle.md), [Laser Pistol](laser-pistol.md) | [Cutter](cutter.md) | **Heat** (decided) |
| Kinetic | Assault Rifle, Magnum | Daggers | Magazine and reload (proposed) |
| Resonance | Staff, Focus (the healer) | Saber | Undecided |

Every tree page is **Claude's draft until Joe edits it or says otherwise**; its status line says which.

## Laser: heat

**Decided by Joe, 15 October 2026.** Laser weapons have no magazine and never reload. Firing builds
**heat**, from 0 to 100; heat drains once a weapon stops firing; at 100 it **overheats** and cannot fire
until it has vented. Each weapon sets its own rates, which is most of what makes them feel different:

| Weapon | Heat built | Drain | Overheated for |
|---|---|---|---|
| Laser Rifle | +8 a bolt, +25 a full beam | 20 a second, after 1 s | 2 s, then resumes at 50 |
| Laser Pistol | +5 a shot, +20 a burst | 30 a second, after 0.5 s | 1.5 s, then resumes at 40 |
| Cutter | +12 a second while cutting, +4 a slash | 25 a second, after 1 s | 2 s, then resumes at 50 |

Heat is the weapon's, not the character's: switching to the other equipped weapon leaves it to drain.
That makes the second weapon a way to cool the first, which the Pistol's Gunslinger tree leans on.

**Scorch** is the family's burn: 2% of the hit's damage a second for 4 s, stacking three times. Every
Laser tree that burns uses it rather than inventing its own, so one status reads the same everywhere.

## Shared ideas still to design

- **Weak points** stay (Joe, 15 October). Creatures and ships have marked weak spots, a critical hit
  where they are hit. **PvE will exist** (Joe, same day) and is not designed anywhere yet: what creatures
  there are, where they live, and what a weak spot looks like on one.
- **Exposed** (seeing a marked target through walls) stays, "fun if done right". In PvP it cuts against
  faction space being defended rather than hunted (ADR-0009), so its range, duration and who sees it want
  designing with that in mind.
- **Blindness and smoke** need designing: what a blinded screen shows and for how long, what smoke hides
  and from whom. Both are interface.
