# Missile Rack — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided** beyond the ship
weapon rules and missiles locking on ([README](README.md#ship-weapons)). Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses; none has been tested.

**Missile family, ship weapon.** Gimballed within about 15°, though a guided missile turns on its own. A rack
of four missiles, reloading one every 2 s, built into the weapon. No ship sibling yet, so it keeps all of
its XP.

## The idea

**Patience and payoff.** Missiles are slow to bring to bear: you hold a target in your sights to **lock
on**, and only then fire something that will find it. A good lock is nearly a guaranteed hit, unless the
target breaks it, outruns it, or shoots it down. Its trees are **Hunter** (one big missile that does not
miss) and **Swarm** (many small ones, many targets).

## Attacks

| Attack | Input | What it does | Rack |
|---|---|---|---|
| **Dumbfire** | Click | A missile straight ahead, unguided, to 3 km. Quick and easy to dodge. | 1 |
| **Lock and fire** | Hold on a target, release | Keep a ship within the gimbal's cone for 1.5 s to lock; releasing fires a guided missile that follows it to 6 km. Releasing early fires a dumbfire. | 1 |

The target is **warned** the moment the lock begins, so it has a chance to break it: flying out of the cone
before 1.5 s breaks the lock.

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Hunter — one missile, one kill

| Node | Kind | Effect |
|---|---|---|
| **Heavy warhead** | Active, 18 s | The next locked missile carries a heavy warhead: triple damage, slower. |
| ↳ Armour piercing | Upgrade | It ignores half the target's armour. |
| ↳ Faster | Upgrade | It is as fast as a normal missile. |
| **Quiet lock** | Active, 22 s | For 6 s your locks give the target no warning. |
| ↳ Quick lock | Upgrade | Locks take 1 s while it lasts. |
| ↳ Long quiet | Upgrade | 9 s. |
| **Seeker** | Active, 16 s | A missile that, if its target is lost, finds the nearest enemy ship within 2 km instead. |
| ↳ Persistent | Upgrade | It may re-target twice. |
| ↳ Smart fuse | Upgrade | It ignores flak and screens once. |
| Steady hands | Passive | Locks take 25% less time. |
| Long reach | Passive | Guided missiles reach 9 km. |
| Weak point | Passive | Missiles that strike engines or hardpoints critically hit, +30%. |
| Agile | Passive | Missiles turn 30% more sharply. |
| **Lights out** | Capstone | A Heavy warhead that hits a ship below 30% hull disables it for 6 s. |

### Swarm — many missiles, many targets

| Node | Kind | Effect |
|---|---|---|
| **Volley** | Active, 16 s | Fires the whole rack at once at the locked target, without spending it. |
| ↳ Wide volley | Upgrade | The missiles spread to strike up to three ships near the target. |
| ↳ Stagger volley | Upgrade | Each missile adds 20 stagger. |
| **Multi-lock** | Active, 20 s | For 5 s you can lock up to four ships at once, and Lock and fire sends a missile to each. |
| ↳ Fast multi-lock | Upgrade | Each lock takes 1 s. |
| ↳ Six | Upgrade | Up to six ships. |
| **Minefield** | Active, 24 s | Drop four missiles behind you that wait 10 s and launch at the first enemy ship within 1.5 km. |
| ↳ Wider field | Upgrade | 2.5 km. |
| ↳ Lasting field | Upgrade | They wait 20 s. |
| Deep rack | Passive | Six missiles. |
| Quick racks | Passive | Missiles reload every 1.5 s. |
| Cluster | Passive | Missiles burst into fragments on hitting, damaging other ships within 200 m. |
| Saturation | Passive | Every missile you have in flight at a target makes it 5% harder for flak to stop the next. |
| **Swarm** | Capstone | Volley fires twice as many missiles and Multi-lock's locks no longer warn their targets. |

## Questions this draft raises

- **The Missile family's effect**: Laser has Scorch and Kinetic Stagger. Missiles here borrow Stagger
  (Stagger volley) and disabling (Lights out); whether they have one of their own is open.
- **Warnings and silent locks**: a lock warning is interface, and Quiet lock and Swarm take it away. In PvP
  that may be the most important single thing on a ship's screen.
- **Minefields** left behind in contested space could linger where nobody meant them to; their lifetime and
  who they hit (friendly ships, protected players) need the PvP rule (`Pvp.CanAttack`) applied to them.
