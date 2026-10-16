# ADR-0015 — Every weapon is a skill, with a tree of its own

**Status:** Accepted · 2026-10-15 · Joe's combat notes of 14 October and his answers of 15 October ·
replaces design-bible §2's `guns` and `melee` · hosted by **M6** in the roadmap · task 192

## Context

Design-bible §2 gave personal combat two skills, `guns` and `melee`, and ship combat two, `lasers` and
`missiles`. Nothing was ever built on them: no weapon exists, nothing awards their XP, and task 101,
which would have seeded them, was waiting on an action to award it.

On 14 October Joe gave the combat milestone its shape, modelled on New World: every weapon levels 0–99
on the same curve as every other skill (ADR-0004), on foot and in ships; weapons of one family and type
share XP, mostly to the weapon used; each weapon has a tree of actives and passives, a basic attack and a
heavy one; skill points come from levelling, all of them by level 50, with titles and skins past that;
and two weapons are equipped at once. On 15 October he settled the open questions that left.

One of them was found by measuring rather than by asking. On the RuneScape curve, **level 50 is 0.78% of
the XP to 99** (101,333 of 13,034,431); level 92 is halfway. A tree complete at 50 would arrive in a
weapon's first hundredth of XP.

## Decision

### A skill per weapon

Every weapon is a skill, levelled 1–99 on the ADR-0004 curve. `guns` and `melee` are not built.

Each weapon skill carries a **family**, a **type** and a **mount**:

| Family | On foot, ranged | On foot, melee |
|---|---|---|
| Laser | Laser Rifle, Laser Pistol | Cutter (a short industrial cutting beam) |
| Kinetic | Assault Rifle, Magnum | Daggers (fast, short, light) |
| Resonance | Staff, Focus (heals) | Saber |

`Force` became **Resonance**, because Saber, Staff and "Force" together read as somebody else's
setting. **Ship weapons** are skills of the same kind, mounted on a ship: a Laser beam, a Kinetic railgun,
and Missiles to begin with; their names are content.

### XP comes from damage, and is shared within family and type

- **Earned for damage dealt**, in proportion to it, as in RuneScape; the Focus earns it for healing done.
  The XP per point of damage is content.
- **80% to the weapon used, 20% divided equally among its siblings**: the other weapons of the same
  family, type and mount. The Laser Rifle's sibling is the Laser Pistol; the Cutter, alone in Laser
  melee, has none. **With no sibling, the weapon keeps all of it**, so a fight is worth the same total
  however many weapons anyone owns, and the integer rounding goes to the weapon used, so nothing is
  created or lost.
- **Ship and on-foot weapons never share**, even within a family: a Laser Rifle does not train a ship's
  beam. Pilot and soldier are different careers.

### Two trees, twenty points, the last at level 70

- **Two themed trees per weapon**, each with actives, passives and a capstone. **Three actives
  equipped** at a time, beside the basic attack (a click) and the heavy one (holding it). Twenty points
  cannot fill both trees, so two players of one weapon can differ.
- **Twenty points, spread evenly from level 4 to level 70**: 4, 7, 11, 14, 18, 21, 25, 28, 32, 35, 39, 42,
  46, 49, 53, 56, 60, 63, 67, 70. **70, not 50**: the tree costs 5.7% of the XP to 99, so building fully
  into one weapon takes real time; 70 to 99 is titles, skins and the like.
- **Two weapons equipped** on foot, switched between. A ship's equivalent is its hardpoints.

### One healer

The **Focus** is the healing weapon: Resonance, ranged, so it shares XP with the Staff. One clear healer,
decided before any tree is drawn, because every other tree is shaped around whether one exists.

## Consequences

Positive:

- **Weapons are something to level, not only something to own.** A player's history with a weapon is
  visible in its level, which a single `guns` skill could never show.
- **The XP split cannot be farmed.** Owning a sibling only redirects a fifth of the XP, so a collection of
  weapons is not a multiplier.
- **Everything is a pure function.** Who shares with whom, how XP divides, and how many points a level
  has unlocked are decided by rules in `SpaceMMO.Domain` with no database, tested like the rest.
- **Weapons are crafted goods with styles** (design-bible §5, ADR-0008, task 187): every weapon comes in
  Common and the four worlds' looks with no further design.

Negative, and accepted:

- **Many skills.** Nine on foot and three ship weapons to begin with, each a row on the skills screen and a
  0–99 grind. The skills screen will want grouping before they arrive, which is interface and Joe's to
  approve.
- **Two trees per weapon is a lot of authoring**: eighteen trees on foot, six more for ships, each with
  actives, passives and a capstone, each to be balanced. This is where the milestone's time will go, not
  the code.
- **Damage-based XP rewards damage**, so the Focus needs its own rule (healing done), and any future
  support weapon that neither damages nor heals (a buffer, a shield) will need one too.
- **The tree is complete at 5.7% of the XP to 99.** 70 to 99 holds no power, by design. If that reads as
  empty in play, the answer is more to unlock there, not moving points.

## Alternatives considered

**Broad skills, `guns` and `melee`** (design-bible §2 as written). Rejected by Joe: a weapon's level is
the point.

**Full XP to the weapon used, plus a share to each sibling.** Rejected: each extra weapon in a family adds
XP to a fight, so owning weapons becomes a way to level faster.

**A 70/30 split.** Rejected in favour of 80/20: training one weapon should mostly train that weapon.

**The tree complete at level 50**, as first written. Rejected once the curve was measured: 0.78% of the XP
to 99. Level 80 (15%) was also offered.

**One tree per weapon**, and **four actives equipped**. Rejected: one tree leaves no choices to make, and
four actives on each of two weapons is more buttons than the basic-and-heavy attack model wants beside it.

**The Staff as the healer, or no healing weapon** (healing from consumables). Rejected for a dedicated
healer, so the Staff stays a damage weapon.

**Ship and on-foot weapons of one family sharing XP.** Rejected: one family of training would make piloting
and fighting on foot one career.
