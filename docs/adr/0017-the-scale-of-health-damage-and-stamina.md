# ADR-0017 — The scale of health, damage and stamina

**Status:** Accepted · 2026-10-16 · Joe's answers of 16 October · resolves design-bible §10's open question
on `constitution` and `stamina` XP (task 102), and the damage scale ADR-0015 and task 192 waited on
· **Amended 2026-10-19** (decision 9, the dodge's timing and reach; Joe's answers, task 195)

## Context

ADR-0015 made every weapon a 0–99 skill earning XP for damage dealt, with the rate "tuned so a weapon
reaches 70 in a planned number of hours" and no numbers behind it: health and damage had no scale, so the
rate meant nothing (task 192). The design bible left `constitution` (maximum health) and `stamina` (the
exertion pool) without an XP source, because they are pools rather than activities; task 102 warned that
changing an XP source after players have XP is a migration and an apology. Sprint was built in August
(task 136) with no pool behind it, left for this milestone.

Everything here is first numbers, set before anything can be shot. They are recorded as an ADR because
two of them are hard to take back once players hold XP and gear: where damage comes from, and where
constitution's XP comes from.

## Decision

Decided by Joe on 16 October, in four rounds, each offered against the alternatives below with Claude's
suggestion first.

### Fights, health and damage

1. **An even fight on foot lasts 8–12 seconds**: same level, full health, nobody missing, no healing. That
   is New World's pace, and it fits the drafted actives' 10–20 s cooldowns: each is used about once a fight,
   and a dodge or a heal can turn it. A tier-1 Laser Rifle does about 110 damage a second against 1,000
   health, about 9 s.
2. **Health is 1,000 at constitution 1, rising with each level to 2,000 at 99.** A veteran is tougher, and
   two newcomers can still bring one down, which keeps faction space defended by everyone (ADR-0009).
   Because the curve is RuneScape's (ADR-0004), most of the doubling arrives in the first half of the XP.
3. **Damage comes from the weapon's tier.** Weaponcrafting makes better tiers of each weapon, and a better
   rifle hits harder; Joe's preview had a tier at weaponcrafting 20 doing 115% and one at 40 doing 130%. A
   player's level in the weapon gives tree points only, as ADR-0015 has it. That keeps weapons worth crafting
   when they are no longer lost on death (ADR-0016).
4. **Hit zones scale damage before armour**: head ×1.5, body ×1, legs ×0.75; from the front or side ×1,
   from behind ×1.2, multiplying. A shot in the back of the head does ×1.8. Armour then protects its own
   zone (task 179).

### Constitution and stamina

5. **Constitution earns a third of every combat XP award, on top.** A hit worth 300 XP gives the weapon its
   240 and its siblings their 60 (ADR-0015), and constitution 100 besides. It is even across weapons
   because weapon XP already is; the Focus's healing XP counts, so healers level it too.
6. **Stamina earns XP for stamina spent**, in a fight or out of one: it levels as you move around the world.
   Running in circles levels it too, slowly, as laps level RuneScape's Agility; accepted.
7. **The pool is 100 at stamina 1, rising to 150 at 99**, and refills at 20 a second after 1 s without
   spending. A veteran sprints or dodges for longer; a newcomer can still get away.
8. **Sprint and the dodge roll spend stamina; jumping is free** (Joe, against a small cost per jump). Sprint
   drains while held, and at zero drops to a run.
9. **The dodge roll is new, on Left Ctrl**: a roll in the direction of movement (backwards if standing),
   about 4 m over 0.5 s, untouchable for the first 0.25 s, costing 25 stamina, so four from a full pool at
   level 1. New World's dodge. Left Ctrl is free on foot; in a ship it is thrust down, and nobody is both.

   **Amended 19 October (task 195).** Made in Blender, a roll squeezed into 0.5 s read as "comical ... so fast and
   unnatural" (Joe): no person rolls in half a second. So **the roll is the animation library's motion-captured
   roll** over the shoulder and up, about **1.1 s**, carrying the character **2.9 m** at the capture's own speed
   through it, so the body never slides on the ground; the dodger is committed until it ends. **Standing still it is
   a backstep hop**, 0.5 s and about **1.5 m** back, made in Blender with 20 cm of air. Still untouchable for the
   first 0.25 s, still 25 stamina. Offered against keeping 4 m for both (with the slide), and against keeping 0.5 s
   with a dive-dash in place of the roll.

### The XP rate

10. **A weapon reaches level 70 in about 50 hours of fighting with it.** On the RuneScape curve 70 is
    737,627 XP, so 99, for titles and skins only, is about 880 hours, and constitution reaches 70 after
    about 120–150 hours of fighting. The XP per point of damage follows from the damage a player really
    deals in an hour, which nothing can measure yet: at 110 damage a second, fighting a third of the time,
    about 0.12 XP per point before the 80/20 split. It is set when the first weapon fires and tuned against
    what it measures. (The figure in the question Joe answered, ~15 XP a point, was a hundred times too
    high, and was corrected in the conversation; the decision was the hours.)

## Consequences

Positive:

- **Every number a weapon needs exists**: what it hits for, what it hits, how long that takes, and what it
  earns. The first weapon can be built.
- **Constitution can't be farmed separately** from fighting, and can't run ahead of the weapons, since it is
  a fixed share of what they earn.
- **Weaponcrafting has a reason to exist at every level**, which matters more now that weapons are kept.
- **Sprint finally costs something**, and the dodge gives the 8–12 s fight the timing it was sized for.

Negative, and accepted:

- **All of it is first guesses**, and fight length depends on accuracy, cover and abilities that no number
  here models. The rate and the damage will move once something can be shot at.
- **Tiers are undesigned.** How many, what each needs, and what separates a tier-3 rifle from a tier-1 one
  besides damage are open, and so is whether armour has tiers the same way (179).
- **A gap between levels 92 and 99 is almost nothing**, and between 1 and 50 almost everything, because of
  the curve. That is RuneScape's shape and was chosen with it (ADR-0004).
- **The dodge's untouchable moment is a networking problem**: the server decides hits (ADR-0003), so it has
  to agree with the client about when a roll began, and in PvP a quarter of a second is contested.
- **Running in circles levels stamina.** Slowly, and accepted, as Agility's laps are.
- **PvE has no scale yet.** Creature health and damage will have to be set against these numbers when
  creatures are designed.

## Alternatives considered

**Fight length:** 3–5 s, a shooter's pace, where position and the first shot decide and most actives would
never come off cooldown; 20–30 s, a slower MMO pace where group fights drag and healers can stall a fight.

**Health gap:** half as much again by 99, where gear and weapons decide more than time played; ten times,
RuneScape's Hitpoints from 100 to 990, where a veteran cannot be touched by newcomers and the game would need
the level brackets it does not have.

**Damage:** from the player's level in the weapon, rising to 99, which ADR-0015 had ruled out and which
makes every rifle the same; both, the tier mostly with up to 15% from level by 70.

**Hit zones:** head ×2, legs ×0.5, back ×1.5, where headshots decide fights; no multipliers, where zones
matter only through armour and facing does nothing.

**Constitution XP:** from enemy damage taken and survived, which makes a creature that cannot kill you a
training dummy; both a share of combat XP and damage taken.

**Stamina XP:** a share of combat XP only, so a hauler who runs everywhere never runs better; stamina spent
only in fights.

**The pool:** doubling by 99, matching constitution; the same pool for everyone with a faster refill.

**What spends stamina:** jumping, rejected by Joe; heavy attacks and blocking, which would double up on the
weapons' own heat, rounds and charge.

**Dodge key:** a double-tapped direction, which causes accidental dodges and delays every one; Left Alt,
which orbits the camera (task 138). **The roll:** without the untouchable moment; a 2 m sidestep.

**Hours to 70:** about 25, where trying a weapon is a weekend; about 100, where most players master one or
two.
