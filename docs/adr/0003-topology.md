# ADR-0003 — Server-per-system topology, with an authoritative backend monolith

**Status:** Accepted · 2026-07-29 · **amended by
[ADR-0007](0007-one-handcrafted-system.md)**

> ADR-0007 makes one system the whole game, so there is one UE dedicated server and
> the warp handoff in point 1 is out of scope rather than deferred. Points 2 and 3 —
> `system_id` plumbed everywhere, persistence in the backend monolith and never in UE
> — stand unchanged and are why this remains worth reading.

## Context

Unreal's replication handles roughly 100–200 players in one map with careful tuning.
That is a session-based shooter's budget, not an MMO's. Meanwhile a player-driven
economy means player wealth is the single most attack-worthy thing in the game, and
any dupe bug is a currency printer.

Two separate problems, often conflated:

1. **Simulation scale** — how many players can share a space and see each other move.
2. **Persistence and authority** — who owns the truth about wealth, skills, and items.

Solving them with the same technology is the standard mistake. UE is excellent at the
first and unsuitable for the second: a UE server crash must never be able to destroy
or duplicate a player's assets.

## Decision

**Design for one UE dedicated server per star system. Build a single system first.**

```
   Clients ──▶ API monolith ──▶ Postgres
                    │              ▲
                    ▼              │
              UE Dedicated Server ─┘
```

Three parts:

1. **Star system is the shard boundary.** One UE dedicated server per active system,
   spun up on demand and idled down when empty. This matches EVE's node-per-system
   model, and it matches the fiction: warp drive between systems is a server handoff,
   covered by a 2–5 second warp animation so it reads as diegetic rather than as a
   loading screen.

2. **`system_id` is plumbed through every table and endpoint from the first
   migration**, even though only one system exists. Sharding then becomes a deployment
   change rather than a schema rewrite. This is the cheap decision that keeps the
   expensive option open.

3. **Persistence and economy live in an ASP.NET Core monolith**, never in UE. One
   process, with enforced internal module boundaries (`Domain` has no I/O
   dependencies). Not microservices.

## Consequences

Positive:

- Simulation load scales horizontally by adding systems, which is the axis the game
  naturally grows along.
- Player wealth survives any UE server crash, because UE never owned it.
- The economy is testable, and balanceable, with no engine involved at all — which is
  what makes M1 possible before UE is even installed.
- One deployable backend process is operable by one person.

Negative, and accepted:

- Cross-system interaction (a market order visible from another system, chat, fleets)
  requires backend mediation rather than UE replication. Correct, but more code.
- The handoff on warp is a real disconnect/reconnect. It must be fast and reliable, or
  the game's central verb feels broken.
- Every economy action costs a round trip from UE server to backend. Latency budgets
  must be explicit, and hot paths need caching (Redis, when justified — not yet).
- A monolith will eventually need splitting. Accepted: module boundaries make that
  mechanical, and premature splitting is the more expensive error.

## Alternatives considered

**Single seamless world across many UE servers with zone handoff.** The Star Citizen
ambition. Rejected: server-meshing is an unsolved-in-practice problem that has
consumed hundreds of engineer-years elsewhere.

**Persistence inside the UE server, saved to disk.** Rejected outright. A crash or an
exploit becomes item duplication, and item duplication is fatal to a player-driven
economy.

**Microservices from day one** (separate gateway, market, persistence). Rejected on
team-size grounds: three services means three deployments, three failure modes, and
distributed transactions across the ledger — for one developer, before any players.

**Authoritative-server-per-region rather than per-system.** Deferred. If systems turn
out to be too small a unit to be worth a process, several systems can share one server
without any schema change — which is precisely what point 2 preserves.

## Notes

Do **not** build the warp handoff during M1–M3. One system is enough content for
years of solo development, and the handoff is meaningless until there is a second
system worth visiting.
