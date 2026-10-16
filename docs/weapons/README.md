# Weapons

The design of each weapon, per [ADR-0015](../adr/0015-every-weapon-is-a-skill.md) and task 192. Each weapon
has a page with its basic and heavy attacks and its two trees; this page holds what a whole family shares.

| Family | On foot, ranged | On foot, melee | Mechanic |
|---|---|---|---|
| Laser | [Laser Rifle](laser-rifle.md), [Laser Pistol](laser-pistol.md) | [Cutter](cutter.md) | **Heat** (decided) |
| Kinetic | [Assault Rifle](assault-rifle.md), [Magnum](magnum.md) | [Daggers](daggers.md) | **Rounds** (decided) |
| Resonance | [Staff](staff.md), [Focus](focus.md) (the healer) | [Saber](saber.md) | **Charge** (decided) |

And on ships ([below](#ship-weapons)):

| Family | Ship weapon | Mechanic |
|---|---|---|
| Laser | [Beam Laser](beam-laser.md) | Heat, as on foot |
| Kinetic | [Railgun](railgun.md) | Rounds, as on foot |
| Missile | [Missile Rack](missile-rack.md) | Lock-on and a rack |

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

## Kinetic: rounds

**Decided by Joe, 15 October 2026**: *"Rounds is the right mechanic- ammo does not need to be crafted or a
separate item, just baked into the weapon. Everything else sounds great!"* Proposed by Claude the same day.
Where a Laser weapon is limited by heat, a Kinetic
weapon is limited by **rounds**: a count it carries, spends, and has to restore. The guns hold a magazine
and reload, which is a pause you choose; the Daggers carry throwing blades that come back to the sheath on
their own. Running dry is the Kinetic player's version of overheating, and choosing when to reload is the
skill.

| Weapon | Rounds | Restored by |
|---|---|---|
| Assault Rifle | 30 a magazine | Reloading, 2 s |
| Magnum | 6 in the cylinder | Reloading, 2.5 s, or one round at a time, 0.4 s each |
| Daggers | 4 throwing blades | One returns every 3 s, or all of them when picked back up |

**Stagger** is the family's effect, as Scorch is Laser's: every Kinetic hit adds stagger to its target,
heavy hits more, and at 100 the target is **staggered** for 0.75 s, interrupted and unable to sprint.
Stagger drains when they stop being hit. Laser burns; Kinetic knocks people about.

**Rounds are not an item.** Ammunition is part of the weapon: a reload is free and draws on nothing in
the inventory, so rounds are a rhythm, not a cost. Asked as a choice against a crafted ammunition
consumable, which would have been a steady material sink; whatever combat consumes is now for the death
rethink (task 103) to say.

## Resonance: charge

**Decided by Joe, 15 October 2026**, from three offered (charge built by hits, a rhythm, an energy pool).
The opposite of heat: a Resonance weapon **starts empty and builds charge, 0 to 100, by landing hits**
(by healing, for the Focus). The heavy attack and several actives **spend** it, and it fades once you are
out of a fight. Laser is held back by what it builds; Kinetic by what it spends; Resonance is pushed on
by what it builds and chooses when to spend it.

| Weapon | Charge built | Spent on |
|---|---|---|
| Staff | +6 a hit | Its heavy wave, and some actives |
| Focus | +1 for every 2% of a target's health healed | Its heavy bloom, and some actives |
| Saber | +8 a hit | Its heavy cleave, and some actives |

Charge fades at 5 a second after 4 s without landing a hit (or a heal). Like heat, it is the weapon's: the
other equipped weapon keeps its own.

**Dissonance** is the family's effect, beside Scorch and Stagger, also decided by Joe on 15 October (from
Dissonance, Echo and a resonant link). Each stack lowers the damage and healing the target deals by 5% for
5 s, stacking three times. **Harmony** is its mirror, applied by the Focus to allies: each stack raises
the damage and healing they deal by 4% for 5 s, three times. Laser burns, Kinetic interrupts, Resonance
weakens and strengthens.

## Ship weapons

**Decided by Joe, 15 October 2026**, each offered against alternatives:

- **Gimballed.** A ship's weapons point forward but swivel within a cone of about 15° toward the reticle:
  you fly to fight, without having to point the nose to the pixel at the speeds the flight model allows.
  Offered against fixed-forward and turrets.
- **Hardpoints vary by hull**, fitted at a hangar: the shuttle has one, the freighter two, and ships yet to
  be designed more. **One weapon fires at a time, switched between as on foot**, so a ship weapon's basic
  and heavy attacks and its three equipped actives work as a rifle's do. Offered against two on every
  ship, and every hardpoint firing together.
- **Families carry over.** A Beam Laser builds heat and a Railgun spends rounds, by the family rules above;
  **missiles lock on**, then fire from a rack that reloads. Offered against one ship-wide energy pool.
- **The shuttle is armed**, with one light hardpoint: enough to answer back on the run to the Capital, which
  new-player protection already covers (ADR-0008). Offered against an unarmed shuttle.

Ship and on-foot weapons never share XP (ADR-0015). With one ship weapon in each family so far, none has a
sibling, so each keeps all of its XP until a second Laser, Kinetic or Missile ship weapon arrives.

**Disabling, not only destroying.** ADR-0006 gives a pirate a reason to disable a ship rather than
obliterate it: disabling preserves most of the cargo. So ship weapons can damage a ship's **systems**
(engines, weapons, the hold's door) as well as its hull, and the Railgun's Disabler tree is built on that.
Whether it still matters depends on the death rethink (task 103), since cargo is what is lost either way.

**Stagger on a ship is a jolt**: at 100, the ship is knocked off its line, thrust and turning cut for
0.75 s. **Scorch** burns the hull as it burns a body.

**Open for ships:** hardpoint **sizes** ("light" implies them; until decided, every ship weapon fits every
hardpoint); **countermeasures** against missiles (a module, a skill, or the Railgun's point defence);
whether the **Missile family** has an effect of its own beside Scorch and Stagger; and what an **on-foot
weapon does to a ship**, and a ship weapon to someone on foot.

## Shared ideas still to design

- **Weak points** stay (Joe, 15 October). Creatures and ships have marked weak spots, a critical hit
  where they are hit. **PvE will exist** (Joe, same day) and is not designed anywhere yet: what creatures
  there are, where they live, and what a weak spot looks like on one.
- **Exposed** (seeing a marked target through walls) stays, "fun if done right". In PvP it cuts against
  faction space being defended rather than hunted (ADR-0009), so its range, duration and who sees it want
  designing with that in mind.
- **Blindness and smoke** need designing: what a blinded screen shows and for how long, what smoke hides
  and from whom. Both are interface.
