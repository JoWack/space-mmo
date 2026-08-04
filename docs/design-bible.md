# SpaceMMO — Design Bible

The single source of truth for game content. Anything in this document that appears
as a table is intended to become `data/*.json` verbatim; writing it down here *is*
the implementation spec.

Names marked `TODO(name)` are placeholders awaiting a decision. Keys are stable and
safe to reference in code — **never rename a key to match a display name**, because
keys end up in the database.

---

## 1. Races and factions

Players choose a race at character creation. Race determines faction and starting
planet; neither is changeable later.

| Race key | Race | Faction key | Starting body key | Planet character |
|---|---|---|---|---|
| `humanoid` | Humanoid | `faction_a` | `body_terra` | Earth-like: temperate, oceans, familiar |
| `martian` | Martian | `faction_a` | `body_ares` | Mars-like: thin atmosphere, red desert, low gravity |
| `space_elf` | Space Elf | `faction_b` | `body_verdance` | Lush, luminous, quasi-mystical — *Gaia*-inspired, a planet that reads as alive |
| `space_orc` | Space Orc | `faction_b` | `body_grimhold` | Dark, industrial, grimy; heavy gravity, perpetual overcast |

- `TODO(name)`: display names for all four planets.
- `TODO(name)`: display names for `faction_a` and `faction_b`.

All four starting bodies sit in the **starting system** (`system_origin`), alongside
the **capital world** (`body_capital`) — a neutral hub hosting the global market,
spaceports, housing, and the career questline givers.

This system is the whole game, permanently; there is no procedural galaxy behind it
(see [ADR-0007](adr/0007-one-handcrafted-system.md)).

### Faction implications

Decided in [ADR-0008](adr/0008-factions-pvp-and-markets.md); this section previously
deferred them.

- **Space is split down the middle.** One half belongs to `faction_a`, the other to
  `faction_b`, and PvP is enabled against the opposing faction in their space. The
  capital sits on the line inside a neutral safe sphere.
- **Legality is a pure function** of the two characters' factions and a position —
  no zone actors, no regions table. It lives in `SpaceMMO.Domain`.
- **Raw materials are planet-locked.** Each homeworld yields ores that exist nowhere
  else, which is what makes hauling a profession and gives the line something to
  divide.
- **Early and mid-tier crafting is self-sufficient within a race**, so a new player
  never stalls on an empty market. **High-tier recipes require materials from all four
  planets** — that requirement is the reason the global market and the PvP zone both
  exist, and removing it would quietly turn the split system into scenery.

---

## 2. Skills

RuneScape-style: levels 1–99, XP curve per [ADR-0004](adr/0004-progression-curve.md),
13,034,431 XP at level 99. Skills are intentionally grindy; each unlocks items and
capabilities at threshold levels.

Three categories. The M1 slice implements only the skills marked **[M1]**.

### Life skills (`life`)

| Skill key | Name | Role |
|---|---|---|
| `gathering` | Gathering | **[M1]** Hand-collecting surface materials; the very first skill any player uses |
| `mining` | Mining | **[M1]** Tool-gated ore extraction from deposits and asteroids |
| `refining` | Refining | **[M1]** Raw ore → refined materials |
| `toolcrafting` | Toolcrafting | **[M1]** Tools, which gate other gathering skills |
| `shipcrafting` | Shipcrafting | **[M1]** Hulls, ship components, whole ships |
| `woodcutting` | Woodcutting | Organic material harvesting |
| `cooking` | Cooking | Consumables, buffs |
| `armorcrafting` | Armorcrafting | Personal armor |
| `weaponcrafting` | Weaponcrafting | Personal weapons |
| `electronics` | Electronics | Ship modules, sensors, computers |
| `construction` | Construction | Player housing, station modules |

### Combat skills (`combat`)

| Skill key | Name | Role |
|---|---|---|
| `guns` | Guns | Ranged personal combat |
| `melee` | Melee | Close personal combat |
| `constitution` | Constitution | Max health |
| `stamina` | Stamina | Sprint, jump, exertion pool |

### Pilot skills (`pilot`)

| Skill key | Name | Role |
|---|---|---|
| `ship_handling` | Ship Handling | Maneuver, speed, fuel efficiency; gates hull classes |
| `lasers` | Lasers | Energy ship weapons |
| `missiles` | Missiles | Guided ship ordnance |
| `warp` | Warp | Warp range, spool time; gates inter-system travel |

**Note on `constitution` and `stamina`:** these are pools, not activities, so they
need a defined XP source before they're implemented — most likely awarded passively
alongside combat actions, as RuneScape does with Hitpoints. Deferred to the combat
milestone.

---

## 3. Item taxonomy

Item categories, which drive UI grouping and station storage rules:

| Category key | Meaning | Stackable | Has condition |
|---|---|---|---|
| `raw` | Unprocessed gathered material | Yes | No |
| `refined` | Processed intermediate | Yes | No |
| `component` | Manufactured part | Yes | No |
| `consumable` | Single-use | Yes | No |
| `tool` | Equipment gating a gathering skill | No | Yes |
| `module` | Ship-fittable equipment | No | Yes |
| `armor` | Personal protective equipment | No | Yes |
| `weapon` | Personal weapon | No | Yes |
| `hull` | Ship frame | No | Yes |

Every item carries a `volume_m3`. Cargo capacity is volumetric, not slot-based —
this is what makes bulk hauling a real profession and gives freighters a purpose.

**Stackable and non-stackable items are stored differently.** Stackable items are
`(item_def, qty)` pairs. Non-stackable items get per-instance rows carrying
`condition` (0–100) and `acquisition_value`, because death resolution can leave an
item *damaged* rather than destroyed, and insurance payouts are pegged to what a
specific hull actually cost. See [economy-design.md](economy-design.md) §3a–3b.

The repair loop that `condition` implies — a repair skill, material costs, and
probably a condition ceiling that degrades per repair so items eventually die anyway —
is **deferred past M3**. The column exists from the first migration regardless, since
adding one to a table full of live player items is far worse than carrying an unused
one.

---

## 4. The onboarding questline (`main_story`)

The new-player experience, and simultaneously the M1 vertical slice of the economy.
The player begins on their race's starting planet and ends flying a ship they built
themselves to the capital world.

Every step is server-validated. Quest credit rewards are the game's **only credit
faucet** — see [economy-design.md](economy-design.md).

| # | Quest key | Objective | Teaches | Reward |
|---|---|---|---|---|
| 1 | `intro_gather_scrap` | Gather 10 `scrap_alloy` by hand from surface debris | Gathering; the world has resources | 500 cr, 200 `gathering` XP |
| 2 | `intro_craft_tool` | Craft 1 `crude_mining_laser` from 8 `scrap_alloy` | Crafting; tools are made, not given | 750 cr, 300 `toolcrafting` XP |
| 3 | `intro_mine_ore` | Mine 20 `ferrite_ore` using the crude mining laser | Tools gate gathering | 1,000 cr, 500 `mining` XP |
| 4 | `intro_refine_plate` | Refine 20 `ferrite_ore` into 4 `ferrite_plate` | Refining; industry jobs take time | 1,250 cr, 600 `refining` XP |
| 5 | `intro_build_hull` | Build 1 `shuttle_hull_section` from 4 `ferrite_plate` + 2 `scrap_alloy` | Manufacturing a real ship part | 2,000 cr, 900 `shipcrafting` XP |
| 6 | `intro_assemble_ship` | Assemble `hull_shuttle` from 1 `shuttle_hull_section` + 1 `crude_thruster` | You built your own ship | 2,500 cr, 1,200 `shipcrafting` XP |
| 7 | `intro_fly_to_capital` | Fly to `body_capital` and dock | Flight, navigation, docking | 5,000 cr |

**Total one-shot faucet: 13,000 credits per character.** This number matters — it is
the entire starting money supply per player, and every price in the early game must
be sane relative to it.

On arrival at the capital, career questline givers unlock. Career chains are **not**
designed yet; they are the natural M4 content milestone.

### Objective types the quest engine must support

`gather` · `craft` · `refine` · `travel` · `dock` · `talk`

That set covers the whole onboarding chain, which is why the chain is a good forcing
function for the engine's design.

---

## 5. M1 content: items and recipes

The complete item set for the onboarding chain. This is `data/items/` and
`data/recipes/`.

### Items

| Key | Name | Category | Volume m³ | Notes |
|---|---|---|---|---|
| `scrap_alloy` | Scrap Alloy | `raw` | 0.1 | Hand-gatherable; the bootstrap material |
| `ferrite_ore` | Ferrite Ore | `raw` | 0.4 | Requires a mining tool |
| `ferrite_plate` | Ferrite Plate | `refined` | 0.2 | Refined; denser than its inputs by design |
| `crude_mining_laser` | Crude Mining Laser | `tool` | 2.0 | Gates `mining` |
| `crude_thruster` | Crude Thruster | `component` | 5.0 | Buyable at start; not craftable in M1 |
| `shuttle_hull_section` | Shuttle Hull Section | `component` | 20.0 | First real manufactured good |
| `hull_shuttle` | Shuttle | `hull` | 200.0 | The player's first ship |

`crude_thruster` is deliberately **purchasable from a faction supply order rather
than craftable** in M1. It gives the market something to do on day one, and it is
the one intentional exception to "everything is player-made" — flagged for removal
once `electronics` exists and players can manufacture thrusters.

### Recipes

| Output | Qty | Skill | Level | Job time | Inputs |
|---|---|---|---|---|---|
| `crude_mining_laser` | 1 | `toolcrafting` | 1 | 30 s | 8 × `scrap_alloy` |
| `ferrite_plate` | 4 | `refining` | 1 | 60 s | 20 × `ferrite_ore` |
| `shuttle_hull_section` | 1 | `shipcrafting` | 5 | 300 s | 4 × `ferrite_plate`, 2 × `scrap_alloy` |
| `hull_shuttle` | 1 | `shipcrafting` | 10 | 900 s | 1 × `shuttle_hull_section`, 1 × `crude_thruster` |

Level requirements are set so the questline's own XP rewards carry the player past
each gate — quest 4 grants enough `shipcrafting` XP context to reach level 5, and so
on. **These numbers are first-draft and expected to change once EconSim runs.** They
live in JSON precisely so that changing them is not a deploy.

### Resource nodes (starting system)

| Body | Item | Qty per node | Respawn |
|---|---|---|---|
| All four starting planets | `scrap_alloy` | 25 | 5 min |
| All four starting planets | `ferrite_ore` | 200 | 20 min |

Identical distributions across the four starting planets, so no race has an economic
advantage at creation. Regional scarcity begins outside the starting system.

---

## 6. Industry jobs

Every recipe in §5 is produced by an **industry job**: materials are committed, the server
starts a clock, and the player returns later to collect. Not an instant craft.

```
Running  ──▶  Claimed      inputs consumed at start, outputs created at claim
   │
   └──────▶  Cancelled     progress-scaled refund
```

Inputs are consumed **at start**, so one pile of ore cannot seed five jobs. Outputs are
created **at claim**, so nobody gets goods before paying the time cost. Both halves are
required; either alone is exploitable.

**XP is awarded at claim, never at start.** Awarding it at start would make start-and-cancel
an XP farm costing only the job fee. This is a constraint rather than a choice.

`CompletesAt` is computed server-side; the client's clock is never consulted.

### Jobs run while logged off

Completion is measured against the server clock, so a 900-second assembly finishes whether or
not the player is connected. This is what makes a RuneScape-depth grind compatible with
having a job, and it is why job durations can be long without being hostile.

### Slots

Concurrent jobs are limited **per skill**, not globally — a refining job consumes a refining
slot.

| Skill level | Slots |
|---|---|
| 1–24 | 1 |
| 25–49 | 2 |
| 50–74 | 3 |
| 75–98 | 4 |
| 99 | 5 |

Per-skill rather than global is what makes specialisation a real choice: a master refiner runs
five refining lines while still limited to one shipcrafting line. Thresholds are spaced by
grind rather than by level number — level 50 is only about 0.8% of the XP needed for 99, so
evenly spaced levels would front-load nearly every reward into the first few hours.

Slots also give cancellation a cost beyond materials: a blocked slot is lost throughput.

### Batch duration is linear

Running a recipe *n* times takes *n* × the recipe duration. There is no batch discount —
batching saves clicks, not time. An economy of scale would favour established industrialists
over new players and add a second number to balance on every recipe.

### Cancelling a job

The refund is proportional to the time **remaining**:

| Cancelled at | Inputs returned |
|---|---|
| 0% | 100% |
| 25% | 75% |
| 50% | 50% |
| 95% | 5% |
| complete | nothing — claim it instead |

One rule covers both cases worth caring about. A misclick caught immediately is forgiven
without needing a special grace period, and a job cannot serve as a free option on the output
price, because backing out late costs nearly the full inputs.

**The job fee is never refunded**, on the same reasoning as the market broker fee: churn has
to cost something.

Rounding is half **up**, which is the opposite of the money rule ([ADR-0005](adr/0005-money-representation.md))
and safe for a different reason — the refund is capped by what was consumed, so it can return
material but never create it. Rounding down instead would make single-unit inputs
all-or-nothing: a hull section cancelled at 5% progress would floor to zero and simply vanish,
which reads as a bug however it is documented.

---

## 7. Gathering

Resource deposits are the **only place material enters the economy**. Everything downstream —
every price, every crafted good — rests on this supply curve.

Gathering is server-authoritative in a specific way: the client asks to gather, and the server
decides how much wall-clock time has elapsed since that character last did. A client calling in
a tight loop extracts exactly as much as one calling at the tick interval.

| Rate | Value |
|---|---|
| Tick interval | 3 seconds |
| Units per tick | 1 at level 1, rising to 4 at level 75+ |
| Banked ticks | 20 maximum |
| XP | 5 per unit extracted |

Yield is **deterministic**, not a per-tick success roll. A roll would be more RuneScape-authentic,
but deterministic output makes EconSim's material-flow measurements depend on the rates being
tested rather than on variance — which matters far more while the economy is being balanced.

XP is per **unit**, not per tick, so levelling does not accelerate itself.

Banked ticks are capped at 20 because gathering is an active verb, unlike industry jobs, which
are deliberately designed to run while logged off. The cap absorbs latency and brief
interruptions without letting an idle client return to claim an hour of extraction it never
performed.

Depleted nodes refill **lazily**, when someone next tries to work them. Nothing observes a node
except a player gathering from it, so a background sweeper would be pure cost.

### Shared versus per-character deposits

**Deposits are shared by default**: one pool everyone draws down, so depleting it denies it to
everyone until it respawns. That is what makes a rich deposit worth reaching first and worth
defending.

The model is stored **per node**, not as one global setting, and both models use the same
depletion table — a shared node has a single row owned by nobody, a per-character node has one
row per gatherer. Switching a node between them is an `UPDATE` on one column, never a migration.

The granularity is the point. If shared deposits turn out to make the starting planets miserable
for new players, those specific nodes can be switched to per-character without giving up
contention in deep space, where competition over good deposits is exactly the intent.

---

## 8. Perspective and controls

Third person by default, with a first-person toggle, both on foot and in ship. The
camera is a client concern only — it must never affect server-side validation, which
is why interaction range is checked against the pawn, never the camera.

---

## 9. Quest kinds

| Kind | Repeatable | Pays credits | Notes |
|---|---|---|---|
| `main_story` | No | Yes — the bootstrap faucet | The onboarding chain in §4, then later story arcs |
| `career` | No | Yes | Unlocked at the capital world; not yet designed |
| `sidequest` | **Yes**, with cooldown | Yes — the steady-state faucet | Subject to the daily credit cap |
| `bounty` | Player-generated | Player-funded transfer, not a faucet | Posted against players who kill in low-security space |

Repeatable sidequests are the ongoing credit faucet, bounded by a per-character daily
credit cap. Once a character hits the cap, sidequests still award XP and items — only
the credit reward is withheld, so the content stays worth doing. Full design in
[economy-design.md](economy-design.md) §2b.

Content for repeatable sidequests is **not yet written** and is not in M1 scope. The
`GrantFaucetCredits` chokepoint and its daily ledger *are* in M1 scope, because every
future credit source must route through them.

---

## 10. Open design questions

1. ~~**Faction warfare rules**~~ — decided in
   [ADR-0008](adr/0008-factions-pvp-and-markets.md); see §1. What remains open is
   *tuning*: the capital's fee premium, flight times between bodies, and which
   high-tier recipes cross faction lines. Those are content numbers, and EconSim
   should settle them.
2. **`constitution` / `stamina` XP sources** — see §2.
3. **Repair loop mechanics** — `condition` is in the schema from the start, but the
   repair skill, its material costs, and whether repeated repairs permanently lower an
   item's condition ceiling are undecided. Deferred past M3; see §3.
4. **Repeatable sidequest content** — the daily cap and the mechanism are designed;
   the actual quests are not written.
5. **Career questline content** — the natural M4 content milestone.
6. **All `TODO(name)` naming** — planets and factions.

### Recently resolved

- ~~Steady-state credit faucet~~ → rate-limited repeatable sidequests under a
  per-character daily cap. [economy-design.md](economy-design.md) §2b.
- ~~Ship destruction and loss~~ → optional tiered insurance paying a percentage of
  recorded acquisition value, plus cause-dependent loot destruction.
  [economy-design.md](economy-design.md) §3a–3b, [ADR-0006](adr/0006-death-and-insurance.md).
- ~~Industry job cancellation~~ → progress-scaled refund, per-skill job slots unlocked at
  levels 25/50/75/99, and linear batch duration. See §6.
