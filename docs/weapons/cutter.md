# Cutter — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided.** Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses; none has been tested.

**Laser family, melee, on foot.** The family's only melee weapon, so it has no sibling and keeps all of its
XP (ADR-0015). Uses the family's heat ([README](README.md)) differently from the guns: heat builds while
the beam is cutting, +12 a second, and +4 a slash; it drains at 25 a second after a second.

## The idea

**The mining laser's violent cousin**: a short industrial cutting beam, a metre or so long, held like a
blade. The first tool a player crafts leads to it, and it should feel like a tool turned to fighting:
it cuts *through* things. The longer it stays on one target the deeper it goes, which is what heat
measures. Its trees are **Breacher** (opening a single target up) and **Slag** (molten metal and holding
ground).

## Attacks

| Attack | Input | What it does | Heat |
|---|---|---|---|
| **Slash** | Click | A short sweeping cut, three in a combo; the third cuts twice as wide. | +4 a slash |
| **Bore** | Hold | Holds the beam on one target in front of you: damage rises the longer it stays on, to double after 2 s. You move at half speed while boring. | +12 a second |

Overheated for 2 s, then resumes at 50.

## The trees

14 nodes each, 28 in all, for 20 points, as the Rifle's and the Pistol's.

### Breacher — cut one thing open

Single targets, armour and weak points. The Cutter's answer to something big: a heavily armoured player,
a creature with a hide, a ship's hatch.

| Node | Kind | Effect |
|---|---|---|
| **Lunge** | Active, 10 s | Dash 8 m forward and slash whatever is at the end. |
| ↳ Follow-through | Upgrade | If the slash hits, the next Bore starts at 1 s of charge. |
| ↳ Reach | Upgrade | Dash 12 m. |
| **Sunder** | Active, 16 s | A deep cut that **opens a weak point** on the target for 5 s: everyone's hits on it count as weak-point hits. |
| ↳ Wide open | Upgrade | The weak point stays open 3 s longer. |
| ↳ Shown | Upgrade | The opened weak point is **exposed** to your allies. |
| **Tether** | Active, 14 s | A cutting line that catches a target up to 10 m away and drags them to you. |
| ↳ Snag | Upgrade | The target is slowed 40% for 2 s afterward. |
| ↳ Reel | Upgrade | Instead of pulling them, pull yourself to them if they are heavier than you. |
| Deep cut | Passive | Bore reaches double damage in 1.5 s rather than 2. |
| Armour cutter | Passive | Your hits ignore 15% of armour. |
| Weak point | Passive | Hits to a weak point critically hit, +40%. |
| Locked on | Passive | While boring, you take 15% less damage from the target you are boring. |
| **Clean through** | Capstone | A Bore held for its full charge on a target below 25% health deals triple damage. |

### Slag — molten metal and holding ground

Area and control at arm's length: the beam spattering molten metal, a fighter who is hard to get past.

| Node | Kind | Effect |
|---|---|---|
| **Slag spray** | Active, 12 s | Throws molten metal in a cone 5 m ahead: damage, and everyone hit is scorched. |
| ↳ Clinging | Upgrade | Anyone hit is also slowed 30% for 2 s. |
| ↳ Wide spray | Upgrade | The cone is half again as wide. |
| **Beam guard** | Active, 18 s | Spin the beam into a disc in front of you for 2 s: it blocks ranged damage from ahead, and every hit blocked adds 5 heat. |
| ↳ Return | Upgrade | Laser bolts blocked are reflected back. |
| ↳ Steady guard | Upgrade | You can walk while guarding. |
| **Slag trail** | Active, 16 s | For 4 s you leave a trail of molten slag behind you that scorches anyone crossing it. |
| ↳ Pooling | Upgrade | The trail lasts 4 s longer on the ground. |
| ↳ Wake | Upgrade | You move 20% faster while laying it. |
| Slag | Passive | Scorched targets are also slowed 10% per stack. |
| Thick skin | Passive | While above 50 heat, take 10% less melee damage. |
| Heat soak | Passive | Slashes add no heat while you are below 30. |
| Spatter | Passive | The combo's third slash scorches everyone it hits. |
| **Forge** | Capstone | While above 70 heat, a fifth of the damage you take becomes heat instead. Overheating while Forge holds vents in a scorching burst around you. |

## What the draft is trying to make possible

- **A Breacher for big targets**: Lunge in, Sunder to open a weak point for the whole group, then Bore.
  The Cutter's reason to exist in PvE, against something armoured.
- **A Slag front-liner**: Beam guard against gunfire, Slag trail and Spray to hold a doorway, Forge to stay
  standing.
- **With a Rifle**: Tether pulls a target into Bore range while the Rifle cools.

## Questions this draft raises

- **Armour** (Armour cutter) assumes armour that reduces damage, and Reel's "heavier than you" assumes
  characters have a weight. Armour is task 179 and has no stats yet.
- **Reflecting laser bolts** (Return) means one Laser weapon counters the others, which may be good or
  may make the family feel unfair against itself.
- **Bore at half speed** makes the Cutter's main damage something you commit to standing for. Too slow
  against players who can roll away (the Pistol's Roll shot)? The trees give it Lunge and Tether to
  answer that.
- **"Cuts through things"** invites cutting hatches, doors or cover. A good PvE idea, and world design
  rather than a tree.
