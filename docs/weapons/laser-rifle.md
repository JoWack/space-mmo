# Laser Rifle — draft trees

**Status: Claude's draft, 15 October 2026, for Joe to edit. Nothing here is decided.** Task 192,
[ADR-0015](../adr/0015-every-weapon-is-a-skill.md). Numbers are first guesses to give the shape weight;
none has been tested in play and combat has no simulator, so expect every one to move.

**Laser family, ranged, on foot.** Shares 20% of its XP with the Laser Pistol (ADR-0015).

## The idea

A precise, sustained energy rifle: no magazine, no reload. **Heat is its resource.** Every shot adds heat,
heat drains when you stop firing, and a rifle that reaches 100 overheats and locks for two seconds while
it vents. The two trees are the two ways to treat heat: avoid it and make every shot count (**Marksman**),
or run hot on purpose and burn things (**Thermal**).

**A family question first, for Joe:** if heat is the Laser family's mechanic, the Pistol and the Cutter
use it too, and Kinetic weapons get something else (a magazine and reloads), as would Resonance (perhaps
charge built by hitting). That gives each family its own feel before any tree is read. Undecided.

## Attacks every Laser Rifle has

| Attack | Input | What it does | Heat |
|---|---|---|---|
| **Bolt** | Click | A fast, accurate single shot. Semi-automatic. | +8 |
| **Focused beam** | Hold, release | Charges for up to 1.5 s; at full charge, triple a bolt's damage and pierces one target. Released early, scales with charge. | +25 at full |

Heat drains at 20 a second, starting a second after the last shot. At 100: **overheated**, no firing for
2 s, then it starts at 50.

## The trees

**14 nodes each, 28 in all, for 20 points**: one tree can be completed, with 6 points left for the
other. A **capstone needs 10 points spent in its tree.** Each active costs 1 point; each active has two
upgrades, a point each, available once the active is taken. **Three actives equipped at a time**, from
either tree.

### Marksman — every shot counts

Distance, weak points and timing. Little heat, little area, high single-target damage.

| Node | Kind | Effect |
|---|---|---|
| **Pinpoint** | Active, 12 s | A focused beam that charges instantly and **marks** its target for 6 s: marked targets take 15% more damage from everyone. |
| ↳ Exposed | Upgrade | The mark also shows the target to your allies through walls. |
| ↳ Shared aim | Upgrade | Allies' hits on a marked target refund them 10% of their cooldowns, once a second. |
| **Steady stance** | Active, 20 s | For 6 s you cannot move, but your bolts add no heat and your range grows 50%. Moving ends it. |
| ↳ Braced | Upgrade | Damage taken while in the stance is reduced 20%. |
| ↳ Quick set | Upgrade | You may take three steps without ending it. |
| **Disruptor bolt** | Active, 15 s | A bolt that interrupts whatever the target is doing and slows them 30% for 3 s. |
| ↳ Feedback | Upgrade | An interrupted ability goes on cooldown for 3 s longer. |
| ↳ Snare | Upgrade | The slow becomes a 1 s root. |
| Long sight | Passive | +10% damage against targets more than 30 m away. |
| Weak point | Passive | Hits to the head (or a ship's or creature's marked weak spot) critically hit, +40%. |
| Cold barrel | Passive | Bolts add 25% less heat. |
| Patience | Passive | The focused beam charges 25% faster. |
| **Killshot** | Capstone | A full-charge focused beam against a target below 30% health deals +50% damage and clears all your heat. |

### Thermal — run hot and burn

Heat as fuel. Burning, area denial and risk: the hotter the rifle, the harder it hits, and the closer it is
to locking up.

| Node | Kind | Effect |
|---|---|---|
| **Sweep** | Active, 18 s | A continuous beam for 2 s, swept with the camera, burning everything it crosses. +40 heat. |
| ↳ Wide arc | Upgrade | The beam is twice as wide. |
| ↳ Afterglow | Upgrade | Anyone the sweep touches is **scorched** twice. |
| **Vent** | Active, 16 s | Dumps all your heat at once in a blast around you: damage scaling with the heat dumped, and a knockback. |
| ↳ Flash | Upgrade | Enemies caught in it are blinded (their screens wash white) for 1.5 s. |
| ↳ Recycle | Upgrade | Each enemy hit takes 2 s off Sweep's cooldown. |
| **Incendiary shot** | Active, 14 s | A bolt that leaves a burning patch 4 m across for 5 s. Anyone standing in it is scorched each second. |
| ↳ Spread fire | Upgrade | The patch grows to 6 m. |
| ↳ Smoke | Upgrade | The patch also blocks sight while it burns. |
| Scorch | Passive | Bolts **scorch**: a burn of 2% of the hit's damage a second for 4 s, stacking three times. |
| Running hot | Passive | Damage rises with heat, to +20% at 80 heat and above. |
| Heat sink | Passive | Overheating locks the rifle for 1 s instead of 2. |
| Thermal shield | Passive | While above 60 heat, take 10% less damage from Laser weapons. |
| **Meltdown** | Capstone | While above 80 heat, every bolt also scorches enemies within 3 m of its target. |

## What the draft is trying to make possible

- **A full Marksman with Vent**: 14 points in Marksman, Vent and one upgrade from Thermal. Reaching
  Killshot, then dumping the heat a long fight still builds.
- **A full Thermal with Pinpoint**: burning a marked target, the team's damage on top.
- **Half and half**: both trees to their passives, no capstone, every active open. Flexible, never great.

## Questions this draft raises

- **Heat as the Laser family's mechanic** (above). If not heat, the Thermal tree has no subject.
- **Blind and smoke** are interface: what a blinded screen looks like, and what smoke hides.
- **"Weak point"** assumes ships and creatures have marked weak spots. Nothing has decided creatures exist;
  PvE is not designed anywhere yet.
- **Ships**: a Laser Rifle fired at a ship. Probably it does almost nothing; worth saying.
- **Seeing through walls** (Exposed) cuts against a game where faction space is "defended rather than
  hunted" (ADR-0009). Possibly wrong for PvP; easy to cut.
