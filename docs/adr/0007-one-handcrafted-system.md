# ADR-0007 — One handcrafted system, not a procedural galaxy

**Status:** Accepted · 2026-08-03 · amends [ADR-0002](0002-generation.md) and
[ADR-0003](0003-topology.md)

## Context

The original plan was a procedurally generated galaxy: thousands of systems produced
by a deterministic function of a seed, with only deltas stored in Postgres. ADR-0002
made that a pure function so the backend and the client could reproduce it
independently; ADR-0003 made the star system the shard boundary so systems could be
spread across processes.

Three things have changed since those were written.

**The generator was never built.** `services/SpaceMMO.UniverseGen` is an empty
directory that was never added to the solution. No golden vectors were generated, and
no C++ port was started. Abandoning procedural generation therefore discards no code.

**The starting system already contains the whole game.** `data/universe/origin.json`
authors five bodies — four race homeworlds around a shared capital — each with a
station. Everything M1 through M3 needs is in there, authored by hand, and it took an
afternoon rather than a subsystem.

**The plan's own risk register was right.** It called seamless procedural terrain "the
classic solo-dev graveyard." A generated galaxy is the single largest source of
schedule risk in the program, and it is also the part that produces the least
gameplay per unit of work: ten thousand generated systems are less memorable than five
places a player can learn by heart.

## Decision

**The game is one handcrafted star system.** Five bodies: the capital world at the
centre, and the four race homeworlds around it. All of it is authored content in
`data/`, none of it is generated.

Consequently:

1. **ADR-0002 is narrowed, not revoked.** Generation-as-a-pure-function no longer
   applies to *macro* content — systems, orbits, and resource distribution are now
   authored. It continues to apply, unchanged, to **planet surfaces**: terrain remains
   a pure height function of a direction vector, sampled identically wherever it is
   asked. That is what keeps the mesh and the collision from ever disagreeing, and it
   is load-bearing for deposit placement today.

2. **ADR-0003's shard boundary collapses to one server.** There is one UE dedicated
   server, permanently. The warp handoff is not deferred any more — it is out of
   scope.

3. **`system_id` stays plumbed through every table and endpoint.** It already exists,
   it costs nothing to keep, and it is the difference between "add a second system one
   day" being a content task and being a schema rewrite. Keeping a cheap door open is
   not the same as planning to walk through it.

## Consequences

Positive:

- Deletes the procedural galaxy, the C++ `UniverseGen` port, the golden-vector
  contract, cross-server warp handoff, and the cross-language float-determinism risk
  that ADR-0002 called out. That is the entire M4 technical spine, removed at a cost
  of zero written code.
- Four planets that must each be distinctive become an *authoring* problem rather than
  an *algorithm* problem — and authoring is the kind of work that can be done in
  small sessions and shipped incrementally.
- Every player is in one place. For a game whose whole point is a shared economy, a
  small population concentrated in one system is far better than the same population
  scattered across a galaxy.

Negative, and accepted:

- The game's sense of scale now has to come from the five bodies being interesting
  rather than from there being many of them. If they are not distinctive, there is no
  fallback.
- Content becomes the bottleneck instead of code. That is the correct bottleneck for a
  game, but it is a genuine shift in what "progress" looks like.
- If the game ever does want more systems, they will be hand-authored too, at roughly
  a constant cost each.

## Alternatives considered

**Keep the generator, ship one authored system first.** Rejected as the worst of both:
it carries the full determinism and porting risk while delivering nothing the authored
system does not already deliver. The generator would sit unfinished and unvalidated
for years.

**Generate the four homeworlds' *terrain* from per-planet seeds.** Not rejected — this
is what already happens, and ADR-0002 still governs it. The distinction that matters
is between generating *where things are* (now authored) and generating *what the
ground looks like at a point* (still a pure function).
