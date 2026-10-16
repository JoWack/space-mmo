# Magnum — draft trees

**Status: Claude's draft of 15 October 2026, approved in outline by Joe the same day** (*"Everything
else sounds great!"*), with the family's rounds and Stagger decided ([README](README.md)). Every number is
a first guess and none has been tested. Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md).

**Kinetic family, ranged, on foot.** Six rounds in the cylinder; a full reload in 2.5 s, or one round at a
time at 0.4 s each, interruptible. Shares 20% of its XP with its siblings,
the Assault Rifle and the Scattergun, 10% each.

## The idea

**Six shots, and each one matters.** The Laser Pistol is fast and mobile; the Magnum is slow and enormous.
Every round hits like a hammer and staggers, so a player who lands their shots can stop someone in their
tracks, and one who misses has a long reload to think about it. Its trees are **Hand Cannon** (the biggest
possible single shot) and **Six-shooter** (the cylinder as something to play with).

## Attacks

| Attack | Input | What it does | Rounds |
|---|---|---|---|
| **Shot** | Click | One heavy round, about three Assault Rifle hits' damage. Adds 35 stagger. Slow to fire again. | 1 |
| **Fan** | Hold | Empties what is left in the cylinder as fast as it will go, with wide spread. Close range. | All left |

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Hand Cannon — one shot, one stop

| Node | Kind | Effect |
|---|---|---|
| **Heavy round** | Active, 12 s | Loads one round that deals double damage and staggers on hit, whatever the target's stagger. |
| ↳ Through and through | Upgrade | It passes through the first target. |
| ↳ Knockdown | Upgrade | The stagger becomes a knockdown: 1.25 s on the ground. |
| **Called shot** | Active, 18 s | Aim at a hit zone for 1 s: a hit on the legs slows 50% for 4 s; on the body, the target's next ability is delayed 2 s; on the head, a critical hit. |
| ↳ Quick call | Upgrade | Aiming takes 0.5 s. |
| ↳ Crippling | Upgrade | The leg slow lasts 6 s. |
| **Warning shot** | Active, 14 s | A round fired near, not at, enemies within 8 m of the impact: they are pushed back 4 m and their sprint is broken. |
| ↳ Rattled | Upgrade | Those pushed have their aim thrown wide for 2 s: double spread. *Replaces Deafening, cut by Joe on 16 October.* |
| ↳ Ricochet threat | Upgrade | The round then bounces into the nearest of them. |
| Big iron | Passive | Shots deal +10% damage. |
| Weak point | Passive | Hits to a weak point critically hit, +50%: the most of any weapon, for the slowest. |
| Brace | Passive | Standing still for 0.5 s before a shot steadies it: no spread and +15% damage. |
| Impact | Passive | Shots add 25% more stagger. |
| **Last word** | Capstone | A shot that staggers someone below 30% health deals triple damage. |

### Six-shooter — play the cylinder

| Node | Kind | Effect |
|---|---|---|
| **Speedloader** | Active, 15 s | Instantly refill the cylinder. Your next shot deals +25%. |
| ↳ Dump and load | Upgrade | Fanning straight into Speedloader refunds 5 s of its cooldown. |
| ↳ Hot load | Upgrade | The refilled rounds add double stagger. |
| **Fan the hammer** | Active, 16 s | A Fan that keeps its spread tight and fires twice as fast. |
| ↳ Steady fan | Upgrade | Every round of the fan adds full stagger. |
| ↳ Spin | Upgrade | You may move at full speed while fanning. |
| **Trick shot** | Active, 20 s | A shot aimed at a hard surface ricochets once toward the nearest enemy within 10 m of the bounce. |
| ↳ Double bank | Upgrade | It may ricochet twice. |
| ↳ Lucky bounce | Upgrade | The ricochet critically hits. *Replaces Around the corner, cut by Joe on 16 October.* |
| Last round | Passive | The sixth round of a cylinder deals +40%. |
| Thumb loading | Passive | Single-round reloads take 0.25 s. |
| Rolling cylinder | Passive | Every third hit refunds a round. |
| Swap and drop | Passive | Swapping away from the Magnum reloads it while it is holstered. |
| **Dead man's hand** | Capstone | Six hits on one target within 3 s stagger them for twice as long, and the sixth always critically hits. |

## What the draft is trying to make possible

- **A Hand Cannon duellist**: Called shot to the legs, Brace, Heavy round, Last word.
- **A Six-shooter brawler** at close range: Fan the hammer into Speedloader, Dead man's hand.
- **With the Assault Rifle**, its sibling: the rifle for the long middle of a fight, Swap and drop
  reloading the Magnum for the finish.

## Decided since, 16 October

- **Deafening and Around the corner are cut** (Joe). Rattled and Lucky bounce stand in for them, Claude's,
  and can be cut too.
- **Hit zones are head, body and legs**, with facing ([README](README.md#combat-rules)), so Called shot's
  arm effect moved to the body.
