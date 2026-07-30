# ADR-0001 — Three-tier coordinate system with local physics grids

**Status:** Accepted · 2026-07-29

## Context

The game requires a galaxy of many star systems, free flight within each system, and
landing on planets without loading screens. That demands positional precision across
roughly fifteen orders of magnitude, from centimetre-scale character collision to
interstellar distance.

Hard constraints:

- UE5's Large World Coordinates makes `FVector` double-precision, supporting a world
  of roughly **88 million km**. One astronomical unit is ~150 million km, so **a
  single to-scale star system does not fit**, let alone a galaxy.
- Chaos physics degrades well before that coordinate limit. Simulation artifacts —
  jitter, tunneling, unstable constraints — appear at far smaller distances from the
  origin than the numeric range suggests.
- Replication of positions costs bandwidth proportional to precision, and an MMO
  cannot afford full doubles for every actor every tick.

This is the most expensive decision in the project to reverse: it touches movement,
replication, physics, rendering, and persistence simultaneously.

## Decision

**A three-tier coordinate model, plus local physics grids.**

| Tier | Representation | Owner | Contents |
|---|---|---|---|
| Galaxy space | `int64`, in the database | Backend | Star system positions. Never an actor, never in UE. |
| System space | `double` km, replicated | UE dedicated server | Planets, moons, stations, ships within one system |
| Local render space | cm, near the world origin | UE client | The subset Chaos actually simulates |

**Local physics grids:** every object declares a parent frame — ship interior → ship
→ planet → star system. Only the player's *active* grid is simulated near the world
origin; every other grid is transformed into view for rendering only. Walking around
inside a moving ship is then ordinary physics in the ship's local frame, rather than
an impossible problem in system space.

**Solar systems are modelled at 1:10 scale.** An Earth-analog becomes ~637 km radius.

## Consequences

Positive:

- Physics always runs near the origin, where Chaos is well-behaved.
- Replication sends local-frame offsets, which are small and cheap.
- Ship interiors work naturally — the classic hardest case becomes the easy case.
- Galaxy scale is unbounded, because galaxy space never enters the engine.

Negative, and accepted:

- Every gameplay system must be frame-aware. "Which grid is this in?" becomes a
  question every feature has to answer, and getting it wrong produces bugs that look
  like physics glitches.
- Transform composition costs CPU per frame per visible grid.
- Grid transitions (walking from a ship onto a planet) are discrete handoff events
  that need explicit, careful code and are a likely source of edge-case bugs.
- 1:10 scale means the universe is not astronomically accurate. This is a gameplay
  win — real distances are unplayably empty — but it forecloses any future claim to
  simulation accuracy.

## Alternatives considered

**Naive single world space at true scale.** Rejected: does not fit in LWC, and physics
would be unusable long before the limit.

**Floating origin only (`UWorld::RebaseOntoNewOrigin`).** Rejected as the primary
mechanism. It solves precision for a single player but interacts badly with
multiplayer, since different clients want different origins, and rebasing does not
solve the moving-reference-frame problem for ship interiors. Origin rebasing may still
be used *within* the local render tier as an implementation detail.

**True-scale with 1:1 distances and long travel times.** Rejected: even with warp,
the empty space between objects at true scale is measured in hours. Elite and EVE both
compress distance for the same reason.

## Notes

This is the first code written in M2, before any gameplay. Retrofitting it after
movement and replication exist would mean rewriting both.
