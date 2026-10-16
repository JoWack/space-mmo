# Scattergun — draft trees

**Status: Claude's draft, 16 October 2026, for Joe to edit. Nothing here is decided** beyond the Kinetic
family's rounds and Stagger ([README](README.md)) and the weapon itself, which Joe added on 16 October.
Task 192, [ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses; none has been
tested.

**Kinetic family, ranged, on foot.** Eight shells in a tube, loaded one at a time, 0.5 s each and
interruptible: ammunition is part of the weapon, like every Kinetic's rounds. Its siblings are the Assault
Rifle and the Magnum, so its XP splits 80% to it and 10% to each of them.

## The idea

**The close-range Kinetic gun.** The Assault Rifle holds the middle distance and the Magnum picks single
shots; the Scattergun wins the room. A cone of pellets that hits hard up close and falls away fast, staggers
everything it catches, and is miserable to walk into. Loading shell by shell means you are always topping
up, never quite full. Its trees are **Brawler** (going in) and **Gatekeeper** (holding a place so nobody
else gets in).

## Attacks

| Attack | Input | What it does | Rounds |
|---|---|---|---|
| **Blast** | Click | A cone of pellets to about 12 m, damage falling off with distance. Each pellet adds 4 stagger, so a point-blank hit staggers on its own. | 1 shell |
| **Choke** | Hold, release | Tightens the pattern while held, up to 1 s: a narrow blast reaching 25 m, every pellet on one target. | 1 shell |

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Brawler — into the room

| Node | Kind | Effect |
|---|---|---|
| **Concussion shell** | Active, 12 s | A blast that throws everyone in a 6 m cone back 4 m and adds 60 stagger. |
| ↳ Floored | Upgrade | Anyone thrown into something solid is knocked down for 1 s. |
| ↳ Wide | Upgrade | The cone is half again as wide. |
| **Slide** | Active, 10 s | Slide 6 m low along the ground, firing a blast when you stop. |
| ↳ Under fire | Upgrade | You take 30% less damage while sliding. |
| ↳ Slide and load | Upgrade | Sliding loads two shells. |
| **Clearance** | Active, 16 s | Turn and fire all round you at once: a blast in every direction to 5 m. |
| ↳ Again | Upgrade | Can be used twice before going on cooldown. |
| ↳ Shaken loose | Upgrade | Everyone hit is staggered. |
| Point blank | Passive | +25% damage within 4 m. |
| Heavy shot | Passive | Pellets add 25% more stagger. |
| Weak point | Passive | Pellets that strike a weak point critically hit, +20% each. |
| Planted | Passive | You cannot be knocked back while firing. |
| **Room clearer** | Capstone | A blast that staggers two or more enemies at once loads two shells and resets Slide. |

### Gatekeeper — nobody gets through

| Node | Kind | Effect |
|---|---|---|
| **Barrier** | Active, 20 s | Plant a kinetic barrier 3 m wide for 6 s. It stops projectiles from the front; you can fire over it. |
| ↳ Wide barrier | Upgrade | 5 m wide. |
| ↳ Lasting | Upgrade | 10 s. |
| **Tripwire shell** | Active, 14 s | A shell fired into the ground that waits 20 s and bursts when an enemy comes within 2 m: damage and 50 stagger. |
| ↳ Two | Upgrade | Two may be set at once. |
| ↳ Snare | Upgrade | Whoever sets it off is rooted for 1 s. |
| **Hold** | Active, 18 s | For 5 s you cannot be staggered or pushed, and enemies within 6 m ahead of you are slowed 20%. |
| ↳ Grit | Upgrade | You take 20% less damage while holding. |
| ↳ Answer | Upgrade | Every hit you take while holding loads a shell. |
| Long tube | Passive | Ten shells. |
| Quick hands | Passive | Shells load in 0.35 s. |
| Shut the door | Passive | Enemies hit by a Blast are slowed 15% for 1 s. |
| Steady choke | Passive | Choke tightens in 0.6 s. |
| **None shall pass** | Capstone | An enemy staggered within 4 m of your Barrier or a Tripwire shell is knocked down for 1.5 s. |

## What the draft is trying to make possible

- **A Brawler** with the Daggers: Slide in, Concussion shell, Clearance when surrounded, Room clearer to keep
  going.
- **A Gatekeeper** in PvE, holding a cave mouth or a corridor for a group, with a Focus behind the barrier.
- **With the Assault Rifle**, its sibling: the rifle on the way in, the Scattergun once inside.

## Questions this draft raises

- **Hold** overlaps the Saber's Stand fast (both make you unshakeable for a few seconds). Two weapons giving a
  front-line role is probably fine; two giving the same button may not be.
- **Barrier** is cover you carry, in a game with no stealth where cover matters more; its health and whether
  it can be shot down are open.
- **Knockdowns** (Floored, None shall pass) are control effects, so the PvP diminishing returns apply.
