# Architecture Decision Records

Each ADR captures one decision that is expensive to reverse, the reasoning behind it,
and the alternatives that were rejected. The point is not ceremony — it is that in six
months the reasoning will have been forgotten, and re-deriving it costs more than
writing it down did.

Format: **Status · Context · Decision · Consequences · Alternatives considered.**
Consequences always include the negative ones. An ADR with only upsides is a sales
pitch, not a decision record.

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-coordinates.md) | Three-tier coordinate system with local physics grids | Accepted |
| [0002](0002-generation.md) | Procedural generation as a pure function; DB stores only deltas | Accepted · narrowed to terrain by [0007](0007-one-handcrafted-system.md) |
| [0003](0003-topology.md) | Server-per-system topology with an authoritative backend monolith | Accepted · shard boundary amended by [0007](0007-one-handcrafted-system.md) |
| [0004](0004-progression-curve.md) | Adopt the RuneScape XP curve unmodified | Accepted |
| [0005](0005-money-representation.md) | Credits as int64 minor units on an append-only ledger | Accepted |
| [0006](0006-death-and-insurance.md) | Cause-based loot destruction; insurance pegged to acquisition value | Accepted |
| [0007](0007-one-handcrafted-system.md) | One handcrafted system, not a procedural galaxy | Accepted |
| [0008](0008-factions-pvp-and-markets.md) | Faction warfare, PvP zoning, and market structure | Accepted |

## When to write one

Write an ADR when a decision is hard to reverse *and* someone could reasonably have
chosen differently. Coordinate systems, currency representation, and server topology
qualify. Naming conventions and library choices usually do not.

Do not edit an accepted ADR's decision. Supersede it with a new one and mark the old
`Superseded by ADR-NNNN`, so the history of the reasoning survives.
