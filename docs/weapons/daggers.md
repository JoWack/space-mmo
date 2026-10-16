# Daggers — draft trees

**Status: Claude's draft of 15 October 2026, approved in outline by Joe the same day** (*"Everything
else sounds great!"*), with the family's rounds and Stagger decided ([README](README.md)). Every number is
a first guess and none has been tested. Joe named them on 15 October: *"Daggers (same as Combat Knife,
but call them Daggers)"*: fast, short, light. Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md).

**Kinetic family, melee, on foot.** The family's only melee weapon, so it has no sibling and keeps all of
its XP. A pair, one in each hand, plus **four throwing blades**: the Daggers' rounds. A thrown blade comes
back to the sheath after 3 s, or at once if you walk over it.

## The idea

**Fast, close, and from the side.** Daggers hit often and lightly: each hit adds little stagger, but a
flurry adds up, and nothing else stays as close to a target or gets out as quickly. Where the Cutter commits
to one target and bores in, the Daggers dart in and out. Their trees are **Flanker** (where you hit from)
and **Skirmisher** (the throwing blades and staying mobile).

## Attacks

| Attack | Input | What it does |
|---|---|---|
| **Flurry** | Click | Quick alternating stabs, four in a combo, each adding 5 stagger; the fourth adds 20. |
| **Throw** | Hold, release | Throws a blade at range, up to 20 m. 15 stagger. Uses one of the four. |

## The trees

14 nodes each, 28 in all, for 20 points, as every weapon's.

### Flanker — from behind, from the side

| Node | Kind | Effect |
|---|---|---|
| **Shadowstep** | Active, 14 s | Dash behind a target within 8 m, facing their back. |
| ↳ Unseen | Upgrade | The target loses track of you: their aim is pushed away from you for 0.5 s. |
| ↳ Twice | Upgrade | Can be used twice before going on cooldown. |
| **Hamstring** | Active, 12 s | A low cut: the target is slowed 40% and cannot sprint for 4 s. |
| ↳ Lame | Upgrade | Their dodges travel half as far while slowed. |
| ↳ Opportunist | Upgrade | Your hits on a slowed target add double stagger. |
| **Puncture** | Active, 16 s | A deep stab that opens a weak point on the target for 4 s, as the Cutter's Sunder does, but only for your hits. |
| ↳ Shared | Upgrade | The weak point is open to allies too. |
| ↳ Twist | Upgrade | Puncture adds 40 stagger. |
| Backstab | Passive | Hits from behind deal +30%. |
| Flank | Passive | Hits from the side add double stagger. |
| Weak point | Passive | Hits to a weak point critically hit, +35%. |
| Light feet | Passive | You move 10% faster with the Daggers out. |
| **Coup de grâce** | Capstone | A Flurry's fourth stab from behind, on a staggered target, deals four times its damage. |

### Skirmisher — the blades and the ground between

| Node | Kind | Effect |
|---|---|---|
| **Fan of blades** | Active, 15 s | Throws all your blades at once in a fan, without using them up. |
| ↳ Wide fan | Upgrade | The fan is twice as wide. |
| ↳ Pinning | Upgrade | Each blade that hits roots its target for 0.5 s. |
| **Recall** | Active, 10 s | Every thrown blade flies back to you at once, cutting anyone in its path. |
| ↳ Return to sender | Upgrade | Blades coming back add double stagger. |
| ↳ Catch | Upgrade | Catching a returning blade makes your next Flurry hit +25%. |
| **Tumble** | Active, 8 s | A quick roll that leaves a blade spinning on the spot, cutting anyone who passes within 2 m. |
| ↳ Caltrops | Upgrade | The spinning blade slows anyone it cuts. |
| ↳ Evasive | Upgrade | You take 30% less damage for 1 s after tumbling. |
| Deep sheath | Passive | Six throwing blades instead of four. |
| Quick return | Passive | Thrown blades come back after 1.5 s. |
| Close and far | Passive | A Flurry hit within 2 s of a Throw hit deals +15%, and the other way round. |
| Balanced | Passive | Throws reach 30 m. |
| **Storm of steel** | Capstone | Every fourth thrown blade is free, and Recall refreshes Fan of blades. |

## What the draft is trying to make possible

- **A Flanker** with the Assault Rifle or Magnum: Shadowstep behind someone the rifle has staggered,
  Coup de grâce.
- **A Skirmisher** who never stops moving: Throw, Tumble, Recall, at the edge of a fight.
- **Puncture with a group**, as a lighter Sunder.

## Questions this draft raises

- **Decided 16 October:** **no stealth** in the game, so "Unseen" and "from behind" stay about position;
  and **facing** is judged for every hit, front, side or back ([README](README.md#combat-rules)), which
  Backstab and Flank rely on.
- **Thrown blades on the ground** are objects others can see; whether an enemy can pick yours up is a small
  question with a large answer.
