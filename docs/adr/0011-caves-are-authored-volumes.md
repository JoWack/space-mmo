# ADR-0011 — Caves are authored volumes, not a density field

**Status:** Accepted · 2026-08-11 · extends [ADR-0002](0002-generation.md) along the
axis [ADR-0007](0007-one-handcrafted-system.md) drew

## Context

Terrain is a pure height function of a direction vector. Everything that touches the
ground asks it the same question and gets back one number:

| Consumer | Asks for |
|---|---|
| Globe mesher | radius along a direction, ~108,000 times per planet |
| Patch mesher | radius along a direction, ~32,000 times per rebuild |
| Ship contact | radius and normal at one direction per frame, **on the dedicated server** |
| Character contact | the same, while walking |
| Deposit and station placement | radius at an authored direction |

That signature is what a cave breaks. A ray from a planet's centre through a cave
crosses the rock more than once, so a function returning a single radius cannot
describe one. This is not an implementation problem that can be worked around; it is
the representation being unable to express the thing.

Two facts constrain the choice, and both were verified rather than assumed:

**The C# backend never evaluates terrain geometry.** It has no noise implementation at
all. `GatherRequest` carries `CharacterId`, `ResourceNodeId` and `StationId` — no
position — and the server grants material on elapsed wall-clock time since that
character last gathered. Terrain is evaluated only in C++, shared by the client and the
UE dedicated server. So the cross-language float-determinism risk ADR-0002 named is not
live here, and cannot be used as an argument either way.

**ADR-0007 already drew the axis.** It distinguishes generating *where things are*, now
authored, from generating *what the ground looks like at a point*, still a pure
function. Caves are awkward because they are both at once.

There is no recorded gameplay requirement for caves anywhere in the design bible. The
requirement that does exist, and that prompted this, is that a cave is somewhere ore
lives and a player goes to get it.

## Decision

**A cave is authored content in `data/`, and the height field stays exactly as it is.**

A cave definition names a body, a place on it, and a shape. Where a cave applies, it
subtracts from the rock the height field describes; everywhere else — which is almost
everywhere — nothing changes.

Consequently:

1. **The height function is untouched**, and so is every consumer in the table above.
   The mesher stays a grid, contact stays a comparison of two radii, and deposit
   placement keeps working the way it does today.

2. **Cave shape may still be a function.** Authored *where*, generated *what* — the
   same split ADR-0007 settled on. A cave's interior can be as procedural as it likes
   inside a volume somebody placed by hand.

3. **A deposit inside a cave takes its height from the cave, never from an authored
   altitude.** `data/universe/origin.json` already states the rule this preserves:

   > No altitude: how high the ground is there is a question the terrain function
   > already answers, and a second answer would be free to disagree with it.

   So a cave deposit carries a direction and a cave reference, and its position is
   derived from the cave's floor exactly as a surface deposit's is derived from the
   height field. One authority per question. Authoring a raw depth alongside a
   direction would reintroduce precisely the disagreement that comment exists to
   prevent, and it would surface as ore embedded in rock the first time a seed changed.

4. **The C# API carries cave references without evaluating them.** A migration and a
   field on the DTO; no geometry crosses the language boundary. Gathering, the market
   and the economy need no change whatever, because none of them know where anything
   is.

## Consequences

Positive:

- Nothing that works today moves. The five consumers of the height function keep their
  contract, and the terrain stack — which cost a great deal to get drawing at all —
  is not rewritten to gain a feature nothing has asked for yet.
- Caves become level design rather than mathematics. That work can be done in small
  sessions, shipped incrementally, and judged by eye, which is the kind of progress
  ADR-0007 deliberately chose.
- The door to a density field stays open. Whatever reads the cave lookup does not care
  how the lookup answers, so replacing authored volumes with a field later is a change
  behind one interface rather than a second rewrite.
- The economy is untouched, so a cave can be added without a schema conversation about
  gathering.

Negative, and accepted:

- **Caves exist only where somebody places them**, at roughly constant cost each. There
  will be no cave over the next hill unless one was authored there, and a planet cannot
  be made interesting underground by turning a dial.
- **Two representations now describe one planet's rock.** They must agree at the
  entrance, and the seam between them is where bugs will live — a cave mouth that does
  not meet the terrain is the obvious first one.
- **The mesher gains a second job.** Punching an entrance into a grid mesh and drawing
  an interior is not free, and the patch mesher is the piece of this codebase with the
  worst track record for rendering surprises (task 84).
- Content is the bottleneck rather than code. That is the correct bottleneck for a
  game, and it is the same trade ADR-0007 accepted knowingly.

## Alternatives considered

**A — A density function; caves fall out of it everywhere.** The intellectually tidy
answer: the surface stops being a height and becomes a field, and overhangs, arches and
caves come free. Rejected for now on cost against a requirement that does not exist yet.
It rewrites both meshers into marching cubes or dual contouring, turns contact from
comparing two radii into stepping a field — per pawn, per frame, on the server — and
needs a new rule for what "on the surface" means for deposit placement when a ray meets
rock several times. Worth revisiting the moment the requirement becomes "ore is deeper
everywhere" rather than "there are caves worth visiting", because at that point
authoring every cave is the bottleneck and this stops being expensive by comparison.

**C — Player-carved voxels stored as deltas.** Fits ADR-0002's "the database stores only
player-caused deltas" exactly, and is the only option that makes digging a verb.
Rejected as premature: it needs A or B underneath it regardless, plus persistence and
server authority over carving, and no design document asks for digging.

**Adopt a voxel plugin (Voxel Plugin Free Legacy or similar).** Rejected, and worth
recording because it is the question that gets asked. A plugin is client-side meshing
and collision; it cannot run in the C# backend, and the dedicated server would have to
be given the same representation by other means. It also brings its own world-coordinate
assumptions, which collide with the render-origin rebasing of ADR-0001, and the free
legacy version is unmaintained. The question "should we use voxels" is really this ADR's
question — what representation do the server and client share — and answering it with a
rendering plugin answers the wrong half.
