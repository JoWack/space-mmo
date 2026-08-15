# ADR-0012 — A ship is earned, summoned, and carries its own hold

**Status:** Accepted · 2026-08-15 · makes [ADR-0006](0006-death-and-insurance.md)'s ship-hold rule
reachable · hosted by **M4** in the roadmap

## Context

Nothing in this game currently owns a ship.

`SpaceMMOGameMode` sets `DefaultPawnClass = ASpaceMMOShipPawn`, so **every connection spawns
flying**. The ship a player pilots is a pawn and nothing else: no row, no item, no record that it
belongs to anybody. A player who has never crafted anything is already in a spacecraft.

Meanwhile the schema has been waiting for the opposite arrangement since the initial migration:

| What exists | What it says |
|---|---|
| `Inventory.ShipItemInstanceId` | "The ship this hold belongs to, for `InventoryKind.ShipHold`. Null otherwise." |
| `ItemCategory.Hull` | Non-stackable, so every hull is an `ItemInstance` with its own condition |
| `shipcrafting` recipes | Already produce Shuttle, Freighter and hull sections |
| `InventoryKind.ShipHold` | Defined, documented, and **created by nothing** |
| `InventoryKind.CharacterCarried` | The same |

`GetOrCreateStationHangarAsync` is the only inventory factory in the codebase. So there is no ship
hold anywhere, for anyone, and there never has been.

This surfaced while building drag-to-transfer for the inventory screen (task 108). Transfer is
addressed by inventory id; with only station hangars existing, and presence enforced per hangar, the
only legal transfer was between two hangars at the same station — a no-op. The feature was
unbuildable, and the reason was not the client.

Two accepted decisions already assume the arrangement that does not exist. [ADR-0006](0006-death-and-insurance.md)
describes a ship's hold as what is inside the explosion when a ship is destroyed, and
[ADR-0008](0008-factions-pvp-and-markets.md) makes four materials planet-locked so that the
cross-faction recipe requires flying them. Neither rule can bite while cargo teleports into a
station hangar and no ship can be lost.

## Decision

**A ship is a thing a player earns, summons, and can lose — and its cargo hold belongs to it.**

1. **Nobody starts with a ship.** A new character begins on foot. A hull is crafted through the main
   questline like any other item, and until then there is nothing to fly.

2. **A crafted hull is summoned into the world** at a docking station or ship hangar. Summoning is
   what turns an owned `ItemInstance` into the pawn a player flies.

3. **A hold belongs to a ship, not to a character.** One `Inventory` of kind `ShipHold` per hull
   instance, keyed by `ShipItemInstanceId` exactly as the schema already describes. Two ships mean
   two holds, and they do not pool.

4. **A hold is reachable only when the player is with it** — docked at a station with their active
   ship, or sitting in that ship. This is the same shape of rule as station presence
   (`RefuseIfNotPresentAsync`): goods are somewhere, and being elsewhere means not having them.

5. **A character carries an inventory of their own**, keyed on the character alone, created at
   character creation. It travels with them, and unlike a hangar it is not somewhere they have to
   be.

## Consequences

Positive:

- **Hauling becomes real.** ADR-0008's planet-locked materials stop being a fact about the database
  and become a reason to fly somewhere with a hold that has a bottom to it.
- **ADR-0006 becomes reachable.** A ship that is an owned instance can be destroyed, and its hold is
  then something to lose. That ADR has been inert since it was accepted.
- **The schema stops lying.** `ShipItemInstanceId` and both unused `InventoryKind` values describe
  the game rather than an intention.
- **Earning the first ship is a story beat** rather than a starting condition, which gives the
  onboarding questline somewhere to lead.

Negative, and accepted:

- **The opening changes completely, and this is the largest cost.** Every connection currently
  spawns in a ship. A player who starts on foot needs somewhere to be, something to do, and a route
  to their first hull — none of which exists. The flight model, the ship pawn and boarding all work;
  what does not exist is a game that starts without them.
- **"Active ship" is new state nobody models.** A character may own several hulls and exactly one is
  the one they are flying or would summon. The hold's accessibility rule depends on it, and so does
  knowing which pawn to spawn.
- **Summoning is a new verb with its own questions** — which station kinds allow it, whether it
  costs, and what happens to a ship left parked somewhere its owner is not.
- **Transfer is limited to carried ↔ hangar until this lands.** That is enough to build and exercise
  the drag interaction, and it is not the feature anyone wants.

## Open questions

Deliberately not settled here, because each is a separate decision and guessing would make this ADR
say more than was decided:

- Where a shipless character starts, and what the first minutes are.
- Whether summoning costs credits, time, or nothing.
- What happens to a summoned ship when its owner logs out, or summons elsewhere.
- Whether a hull must be repaired or fuelled before it can be summoned.

## Alternatives considered

**Grant every character a starting hull.** The one-line version: creation mints a Shuttle instance
and the hold hangs off it. Rejected because it answers the inventory question by removing the thing
Joe wants earned — a ship handed over at creation is a starting condition, and the questline loses
its destination. Worth noting it would have unblocked transfer immediately, which is exactly why it
was tempting.

**Hang the hold off the character, with `ShipItemInstanceId` null.** Also one line, and it works
until a player owns two ships — at which point their cargo pools across a fleet and the schema's own
comment is false. It also puts ADR-0006 permanently out of reach, since a hold that belongs to
nobody's ship cannot be inside an explosion.

**Make the hold reachable from anywhere.** Simpler, and it deletes the reason hauling exists. Cargo
that can be fetched from across the system is a bank account, and ADR-0008's planet-locked materials
become a shopping list rather than a journey.
