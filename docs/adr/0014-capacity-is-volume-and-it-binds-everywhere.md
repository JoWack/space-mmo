# ADR-0014 — Capacity is volume, and it binds on every route in

**Status:** Accepted · 2026-08-31 · gives [ADR-0012](0012-a-ship-is-earned-and-carries-its-own-hold.md)'s
hold a reason to exist · hosted by **M4** in the roadmap

## Context

Task 112 was written on 14 August as "goods are held, not teleported", and most of it has since been
built without the task being updated. Measured today rather than assumed:

| What 112 asked for | What the code does now |
|---|---|
| Gathering deposits into carried inventory | `GatheringService` calls `GetOrCreateCarriedAsync` |
| Crafting draws on carried as well as station stock | `IndustryService` transfers what is missing across, but only while docked at that station |
| Moving goods to a station is deliberate | Transfers are explicit and carry their cost basis |

So goods already stop teleporting. **What is left is the half that makes it matter**, and the code
says so itself, in `InventoryService.TransferAsync`:

> Volume is not checked. `CapacityM3` exists on the row and hangars are created at zero, and nothing
> anywhere enforces it yet; a transfer is the wrong place to invent that rule, because it would apply
> to one route into a hold and not to the others.

Every inventory in the game is therefore infinite. A character carries a planet's worth of ore in
their pockets, a ship's hold is decoration, and hauling — which is M4's whole premise and the reason
ADR-0008 made materials planet-locked — is a formality. Nothing in the game currently distinguishes
a shuttle from a freighter.

The task also recorded Joe's direction that **a character carries 50 kg by default**. That runs into
the one genuine fork: `ItemDef` carries `VolumeM3` and `Inventory` carries `CapacityM3`, and there is
no mass anywhere in the schema.

## Decision

### 1. Capacity is volume. No mass is added.

`VolumeM3` and `CapacityM3` already exist, are already authored per item, and are already documented
as the thing that matters — `ItemDef.VolumeM3` says "cargo capacity is volumetric, not slot-based —
which is what makes bulk hauling a real profession", and `InventoryKind.ShipHold` says "volumetric
capacity set by the hull".

A second dimension would be two systems that can disagree, and 112 named that as a bug generator
before anything was built. **A hauling game needs one number to be interesting**; which of the two it
is changes almost nothing about play, because the dense things are also the bulky things here.

The 50 kg becomes a volume. Nothing is lost: a personal limit expresses perfectly well as a number of
cubic metres, and what a player is shown can say whatever the interface wants it to. Section 5 works
out what that number has to be, and it is not a figure anybody would have guessed.

### 2. It binds on every route in, not on transfers

Enforcement lives in `InventoryService.AddAsync`, which every route already goes through — gathering,
crafting output, market purchase, quest reward, transfer. Putting it on transfer alone would make a
hold you cannot fill by dragging and can fill by mining into it, which is the specific trap the
existing comment warns about.

### 3. Zero still means unlimited, and station hangars keep it

A station hangar is rented storage that already has a sink attached — `InventoryKind.StationHangar`
says it "accrues station rent — the sink that stops it becoming a free infinite warehouse". Capping
it as well would charge rent for a thing that also refuses goods.

**Carried and ship holds get real numbers.** Carried is a flat figure to start;
`ShipHold` takes its capacity from the hull that owns it.

### 4. A full container refuses the whole delivery, and says so

Not a partial fill. Half a delivery arriving and the rest evaporating is a silent loss of a player's
property, and the same rule applied to a market purchase is a silent partial refund. The operation
fails, nothing moves, and the message names the container and how much room is left.

**Amended the same day, on implementing it.** The rule above is right where the goods already exist
and would have to go somewhere or nowhere — a purchase, a transfer, a quest reward. Gathering is not
that case, and writing it before building it missed the difference: **ore that will not fit is still
in the ground.**

A single swing yields twenty ore, which is 8 m³ against a pack of six, so the rule as first written
made mining with a part-full pack fail outright and mining with an empty one fail as well. Gathering
takes **what fits and leaves the rest in the node** — nothing is destroyed, because nothing was
extracted, and the node is not drawn down for material nobody received.

The distinction is whether refusing destroys anything. Where it does, refuse the whole; where the
source keeps what it could not hand over, take what fits.

### 5. The numbers come from the volumes that are already authored

Read out of `data/items/core.json` rather than invented, because they turn out to settle the pace of
the whole loop:

| Item | Volume |
|---|---|
| `scrap_alloy` | 0.10 m³ |
| `ferrite_ore` | 0.40 m³ |
| `ferrite_plate` | 0.20 m³ |
| `crude_mining_laser` | 2.00 m³ |
| `hull_shuttle` | 200.00 m³ |
| `hull_freighter` | 900.00 m³ |

These are authored on a **ship** scale — a unit of ore is 400 litres — and a resource node holds 200
ore, which is 80 m³. Nothing about that is wrong, because what matters is the ratio between an item
and a container and never the absolute figure; but it does mean a personal capacity expressed in
these units is not going to sound like a backpack, and the 50 kg of the original direction was never
going to survive contact with them whichever dimension won.

Settled by Joe, 31 August. The carried figure is the one number here that is taste rather than
arithmetic:

- **Carried: 6 m³** — fifteen ore, or sixty scrap, or three mining lasers. Fourteen trips on foot to
  clear a node, which is the pressure that makes a ship worth crafting without making the on-foot
  loop a punishment before there is anything to fly.
- **A hull carries a hold of its own size**, authored on the hull rather than derived: `ItemDef`
  gains a nullable `HoldCapacityM3`, meaningful for `ItemCategory.Hull` and null elsewhere. ADR-0012
  already says "volumetric capacity set by the hull" and there is nowhere for a hull to say it.
  Shuttle 80 m³ — exactly one node — and freighter 360 m³.

Deriving the hold from the hull's own `VolumeM3` was considered and rejected: it reads as a tidy rule
and means a bigger hull can never be a *worse* hauler, which removes a whole axis of ship design
before any ship exists.

### 6. Capacity from `stamina`, and backpacks, are explicitly not in this

Both were in Joe's 14 August direction. `stamina` does not exist — it is one of the eight skills task
101 seeds, and 101 is blocked on 102 deciding where its XP comes from, which the design bible leaves
open. A flat capacity needs neither, and a skill multiplier is a later change to one number.

### 7. Death rules are not in this either

112 also recorded safe slots, dropping on death, and destruction at 0% condition. Those extend
[ADR-0006](0006-death-and-insurance.md) rather than implement it, and they belong to **M6** with the
rest of death. They want their own ADR when combat is built; folding them in here would put death
rules in a document about capacity, which is how ADR-0006 came to be quietly inert in the first
place.

## Consequences

Positive:

- **A freighter differs from a shuttle**, for the first time, by a number that already exists on the
  schema and needs only to be authored.
- **Hauling becomes a decision.** Planet-locked materials (ADR-0008) become flights with a size,
  which is what M4 promised.
- **The ship hold ADR-0012 built acquires a purpose.** Today it is an empty container with no
  advantage over pockets.
- **One dimension, one rule, one place.** Every route in is checked because they share a door.

Negative, and accepted:

- **Every item's `VolumeM3` starts mattering**, and they were authored when nothing read them. The
  reading above is the first time they have been looked at as a set, and they hold together — but a
  test that asserts the ratios stay sane, reading the authored pack rather than counting it, is
  cheaper than finding out by playing. `BodyPalettesSuitTheirTerrain` is the shape.
- **A migration.** `HoldCapacityM3` is a new column, and the API refuses to start until somebody
  seeds — which is deliberate, documented, and has cost a session before.
- **Existing saves may hold more than the new limits allow.** An over-full container must keep its
  contents and refuse additions rather than destroying anything; over-full is a legal state that
  drains rather than an error.
- **"50 kg" stops being the wording.** A number in a design conversation becomes a different number
  in a different unit, and anybody reading the old task will find it says kilograms.
- **Refusing the whole delivery can lose a gathering swing's cooldown** without giving anything.
  Better than silently destroying ore, and the prompt can say the backpack is full before the swing.
