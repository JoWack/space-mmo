# ADR-0002 — Procedural generation as a pure function; database stores only deltas

**Status:** Accepted · 2026-07-29 · **narrowed by
[ADR-0007](0007-one-handcrafted-system.md)**

> ADR-0007 makes the game one handcrafted system. Systems, orbits, bodies, and
> resource placement are now authored content, so the reasoning below no longer
> applies to them. It still governs **planet surfaces**, where terrain remains a pure
> height function of a direction vector — that part is load-bearing today.

## Context

The universe should contain a very large number of star systems, each with planets,
moons, orbits, and resource distributions. Two naive approaches both fail:

- **Generate once, store everything.** Storing full planetary detail for thousands of
  systems is terabytes of data that is almost entirely untouched by any player.
- **Generate per-session, store nothing.** Then the universe is not persistent, and
  a player-driven economy built on regional resource scarcity is impossible.

There is an additional constraint that is easy to miss and expensive to discover late:
the **backend (C#) and the client (C++) must agree on what the universe looks like**.
The backend needs resource distributions to validate mining; the client needs orbital
geometry to render. If they disagree by even one bit, players see asteroids that the
server says are not there.

## Decision

**Generation is a deterministic pure function; the database stores only player-caused
deltas.**

```
generate(galaxy_seed, system_id) -> star, planets, moons, orbits, resource distribution
```

The database persists only what players changed: structures, depleted resource nodes,
market state, ownership, ship positions. Everything else is recomputed on demand from
the seed. A universe of thousands of systems then fits comfortably in an ordinary
Postgres instance, because unvisited systems occupy zero bytes.

**Cross-language determinism is guaranteed by golden vectors, not by shared code.**
Generation is written first in C# (`SpaceMMO.UniverseGen`), and a committed
golden-vector file records expected outputs for a fixed set of seeds. When the C++
client implementation is written in M4, it must reproduce those vectors exactly, and
CI checks both against the same file.

**Generation paths use integer and fixed-point math only.** No unconstrained floating
point anywhere a generated value is derived.

## Consequences

Positive:

- Storage scales with player *activity*, not universe size.
- The universe is reproducible: a bug report citing a seed is exactly reproducible on
  any machine.
- Content scale becomes essentially free, which is the only way a solo developer ships
  a galaxy.
- The golden-vector file is a hard, cheap, automated contract between two languages.

Negative, and accepted:

- **Changing the generation algorithm changes the universe.** Once players own
  property in a system, the generator for that system is frozen. This forces
  generation to be versioned from the very first commit.
- Writing it twice (C# then C++) is duplicated effort and a permanent source of
  potential divergence. Accepted because an FFI shim is more infrastructure than a
  solo project should carry at this stage, and the vectors catch divergence anyway.
- The integer-math constraint makes generation code noticeably more awkward to write
  than ordinary floating-point noise functions.
- Recomputing on demand costs CPU where storage would have cost bytes.

## Alternatives considered

**Shared C++ library via P/Invoke, used by both backend and client.** The
theoretically correct answer — one implementation, no divergence possible. Rejected
*for now*: it requires a native build pipeline, marshalling layer, and cross-platform
native artifacts before a single line of gameplay exists. Revisit if the golden
vectors prove insufficient in practice.

**Backend-authoritative generation, streamed to the client.** Rejected: the client
needs geometry at rendering rates and detail levels far finer than is sane to send
over a network.

**Rust core with FFI to both sides.** Genuinely elegant, and was seriously considered.
Rejected on team-size grounds: it adds a third language to a solo project.

## Notes

Version the generator (`generator_version` on the `systems` row) from the first
migration. It costs one column now and is unimplementable later.
