# Planned work

The backlog. This file is the authority on what is planned and what is known about it — not the
assistant's in-session task list, which is scratch and does not survive a cleared context. It was
written after exactly that happened: a terrain investigation spanning eight commits and several
playtests came back as "there should be tasks recorded, I believe the terrain work is #84", and
there was nothing to recover.

**Identifiers are permanent and never reused.** Numbers below 87 come from the list that was lost;
where a task has been reconstructed from commits and code rather than recovered, it says so, because
a plausible reconstruction presented as the original is worse than an admitted gap.

Status is one of **pending**, **in progress**, **blocked** (say on what), or **done** (say which
commit closed it, and move it to the bottom).

Milestones live in `README.md` under Roadmap. **M3 is the current one — "closing the loop: mine →
craft → sell, two players trading a player-made item"** — and tasks 91 to 95 are derived from that
sentence rather than recovered from any list. Where a task asserts something is missing, it says
whether that was verified in code or inferred from the design bible, because the two are not the
same and only one of them is safe to act on without looking.

---

## 84 — Find why the terrain patch does not draw

**WORKAROUND TAKEN, MECHANISM UNKNOWN.** `SpaceMMO.PatchFlipWinding` now defaults to 1, so the patch
draws. Nobody knows why it has to. The switch is deliberately left in the planet actor rather than
folded into `FPlanetPatch::Build`, because the generator's output is geometrically correct and the
tests that assert so are correct — inverting them to match what renders would convert a known unknown
into a falsehood the next reader has to unpick. Set it to 0 to restore the fault.

This was a deliberate decision to stop, not a conclusion. The mesher is 263 lines against a height
function of 298 that the server, deposits, stations and both pawns depend on (ADR-0002), so the
throwaway part of this is small and the cost of continuing was mostly playtests.

Everything below is the record of what has been eliminated, written as it was found on 10 August.
Read it as a log rather than as current state: the eliminations hold, but the "next thing to do"
notes inside it were all subsequently done, and their outcomes appear further down.

**The patch reaches the renderer.** Every build after the first reports `has proxy 1`, `registered 1`,
`visible 1`, `actor hidden 0`, `render in main pass 1`, a real material, 32768 triangles, unit scale,
and a camera inside its bounds — so it cannot be frustum-culled. Bounds are geometrically correct
and were checked rather than assumed: a 4° patch on a 20 km planet is ~2.8 km across, and the
reported half-extents (~140,000 cm, diagonal ~205,000 cm) match; the 27.2° patch at startup gives
9.2 km half-extents with a 2 km radial depth, which is the right shape for a cap. `has proxy 0`
appears only on frame 1, before the component has ever rendered.

So everything upstream of drawing is now ruled out, and "the patch does not draw" — reached back when
it was a separate actor — cannot be restated as a fact about geometry reaching the scene.

**With `SpaceMMO.HideGlobeUnderPatch 1` the ground goes pitch black** rather than showing sky or
space through it, while the patch continues to report a proxy and a containing bounds. Wireframe over
the same view showed only a handful of large, sparse quads rather than a dense mesh, which is not yet
explained — though note the horizon on this planet is ~253 m at eye height, so only about a dozen
22 m cells lie between the viewer and it, and "sparse" may be correct rather than wrong.

**Unlit (`ShowFlag.Lighting 0`, globe hidden) did not turn the ground white.** The near ground stays
black while a white band sits at the horizon and changes shape as the viewer turns, and the black
region "follows" the viewer. The patch reports `has proxy 1` and a containing bounds throughout. In
unlit rendering the base colour is drawn with no lighting at all, so a surface carrying
BasicShapeMaterial cannot come out black — which means the black under the viewer's feet is either
not that surface, or not a surface at all. This also rules out lighting, exposure and normals as the
cause of the black, since none of them participate in unlit.

Note the earlier wireframe pass did show large quads in the near field, so geometry of some kind is
present there. Wireframe bypasses materials, unlit does not, and the two disagree.

**With the globe restored and still unlit, the ground turns grey/white.** Same spot, same camera,
same material, same unlit pass: the globe shades correctly where the patch is black. So the fault is
in the patch's own path to the renderer.

**The experiment that sent this investigation after the mesh is unsound.** `PatchIntoGlobe`
(`fc3ce0e`) concluded "the patch does not draw inside the globe's own component, so the fault is in
the mesh rather than in the component" — and it ran two commits before `bffb2ff`, which is the commit
that found these switches were silently not rebuilding and that three variants had been reported as
"did not draw" without ever having been built. That conclusion may rest on a run that never happened,
and everything after it — variants 1 to 4, the registration-timing hypothesis — inherited the
redirection from component to mesh.

`bRecipeChanged` now includes `bWantsGlobeComponent`, so the switch works.

**It was re-run, and stayed black** — confirmed by the log printing `in the globe's component` three
times with `has proxy 1` and the camera inside the bounds. So the pivot was sound after all and the
fault is in the mesh, which the completed 2x2 below then confirmed from the other direction.

Below this point the notes are from before the patch drew at all. At the time it had never appeared
on screen and the globe carried the ground at a vertex every 331 m; both are now false.

Known, from playtests and from source:

- It draws as neither its own actor, nor a second component on the planet actor, nor inside the
  globe's own component (`SpaceMMO.PatchIntoGlobe`). The globe — nearly identical code, same
  component type, same height function — always draws.
- Instrumentation once reported it visible, registered, holding ~32k triangles and a real material,
  with the camera inside its bounds, and never on screen.
- **No patch variant other than 0 has ever been built.** Across all twelve surviving logs that
  contain the line — 6 August 21:09 through 10 August, every one of them later than `bffb2ff`,
  which added per-build variant logging — there are 404 builds and every one reads `Patch variant
  0: as built`. There is no line for 1, 2, 3 or 4 anywhere.

  This contradicts `68511bd`, which states "the log confirms all three were actually built this
  time". No surviving log supports it. The machinery is sound — the variant is logged on every
  build, `AppliedPatchVariant` is assigned, and `PatchIntoGlobe` proved on 10 August that
  `bRecipeChanged` does fire — so the variants were simply never run.

  **The elimination of flat terrain, low resolution and radial normals is therefore void.** Those
  three are untested, not ruled out, as is variant 4. This is the third time in this investigation
  that an experiment reported as a result turned out never to have run.
- The mesh is valid by every measure available without a renderer: no rejected triangle, no
  degenerate face, unit normals, `FDynamicMesh3` validity passing.
- **Variant 4 ran on 10 August and changed nothing.** It built (`anchored at the planet's centre`),
  anchored at `(60.000, 0.000, 0.000) km` with its component 22 km out and its bounds landing on the
  viewer, so its vertices sat ~2,000,000 cm from the component origin exactly like the globe's. Still
  black. **Vertex magnitude and anchoring are eliminated**, properly this time.
- **Variant 1 ran and changed nothing.** Flat, anchored at the nominal radius 340 m below the
  viewer, camera inside bounds, still black. **Elevations are eliminated.**
- **Variants 2 and 3 ran on 10 August and changed nothing** (`low resolution`, `radial normals`),
  so triangle count and vertex normals are eliminated. With variants 1 and 4 that is the entire
  variant set, all four confirmed built by log line, none of them making the ground appear.
- **The globe's winding test uses the identical formula to the patch's** — `cross(B-A, C-A)` dotted
  with the centroid's direction from the planet centre — so both meshes are outward-wound by the
  same measure, and a winding difference in the *data* cannot be what separates them.
- **Visibility tracks slope, and this is the strongest clue yet.** At variant 0 a white band sits at
  the horizon where the ground is steep and distant; at variant 1, with the terrain flat, even that
  band disappears and nothing renders at all. A surface visible only where it is steep, and never
  where it faces the viewer, is what backface culling looks like. The winding tests pass against the
  mesh *data* (`SurvivesBeingWide`, at four widths), so if culling is the mechanism the inversion
  happens after that data and before the rasteriser — in the append into `FDynamicMesh3`, the normal
  overlay, or the component's own transform.

Ruled out on 2026-08-10, without spending a playtest:

- **"Updating a dynamic mesh after registration never reaches the renderer."** False.
  `NotifyMeshUpdated()` calls `ResetProxy()`, which calls `MarkRenderStateDirty()`
  (`Engine/Source/Runtime/GeometryFramework/.../DynamicMeshComponent.cpp`). `SpaceMMO.DirtyAfterMeshUpdate`
  was therefore a no-op asking for work the engine had just done, and has been removed. Had it run,
  "still no ground" would have read as evidence about registration timing rather than about nothing.
- **Winding.** `BuildTangentFrame` is right-handed — `B = Up × T` gives `T × B = Up` — and the globe
  uses an identical index pattern. Now pinned by `SpaceMMO.Patch.FacesOutward`.
- **Units.** `ToLocalCentimetres` does multiply by `CentimetresPerKilometre`.

`SpaceMMO.Patch.FlatPatchFacesOutward` (added 10 August) builds the exact variant 1 geometry — zero
elevation, every vertex on a sphere — and asserts that no triangle faces inward and none is
degenerate. It passes, in a 154-test green suite. So the one case that renders *nothing at all* has
provably correct winding and no zero-area faces, which makes backface culling harder to sustain
rather than easier, and rules out a collapsed flat cap.

**PARTLY RESOLVED, 10 August: reversing the patch's winding makes it draw. Why it needs that, when
the globe does not, is unexplained.**

The claim below that "the globe is wound backwards too" was **disproved by playtest**: with lighting
on, the globe alone (State A: `PatchFlipWinding 0`, `HideGlobeUnderPatch 0` — the patch is invisible
unflipped, so only the globe is on screen) and the patch alone (State B: flip 1, hide 1) both show
correctly lit ground. The globe renders correctly with outward winding. One reported difference worth
keeping: State A has some blue/grey under the pawn where State B is uniformly white.

An earlier attempt at this comparison was worthless and should not be repeated —
`HideGlobeUnderPatch` never hides the *patch*, and a flipped patch spans 1.4 km against a ~253 m
horizon, so it covers every ground pixel in both states.

Also ruled out from engine source: transform-driven reversal. `Mesh.ReverseCulling =
IsLocalToWorldDeterminantNegative()` is the only place the dynamic mesh proxy flips culling
(`BaseDynamicMeshSceneProxy.cpp:482`), and both components are pure translations at unit scale — and
`PatchIntoGlobe` ran the patch through the globe's own component anyway.

**Winding survives the append, for both meshes.** `SpaceMMO.Patch.WindingSurvivesTheAppend` builds a
globe and a patch, appends each into an `FDynamicMesh3` exactly as the actor does, reads the winding
back out of the mesh the renderer would see, and measures both against the planet's centre in the
same units so they are directly comparable rather than each checked against its own frame. Result:
**globe 0 of 1452 inward, patch 0 of 512 inward.** So the two meshes have identical handedness all
the way to the component, and winding is *not* what separates them — which leaves the flip working
for a reason nobody has identified.

**The running game builds the same thing, measured 10 August by a headless probe:**
`globe faces inward: 0 of 108300`, `patch faces inward: 0 of 32768` — full runtime resolution, real
viewer direction, real configuration. So the runtime meshes are identically outward-wound and
**winding is eliminated entirely**. Reversing the patch's indices makes it draw for a reason that is
not its winding relative to the planet, and nothing currently explains that.

Worth keeping for future sessions: this measurement cost no playtest. `UnrealEditor-Cmd.exe ... -game
-nullrhi -ShipStartX=38` runs the client headless, builds globe and patch, and logs whatever they
log — `-nullrhi` crashes the *automation* path on UE 5.8 but is fine here, and the arguments arrived
intact for once (check `LogInit: Command Line:` anyway).

`SpaceMMO.GlobeIntoPatch` fills that cell, added 10 August. It hands the globe's mesh to the patch's
component, hides the globe's own so whatever appears is unambiguously from the patch's, stands the
patch's component at the planet's centre (those vertices are centre-relative, and placing it at a
patch anchor would put the planet 20 km away and produce a black screen for the wrong reason), and
suppresses patch building while it is on so the next tick cannot overwrite the geometry being asked
about. Turning it off rebuilds the globe where it belongs and drops the patch state so the patch
builds again rather than leaving a stale globe underfoot for kilometres.

**The 2x2 is complete, and the component is exonerated.** Globe mesh in the globe's component draws;
globe mesh in the patch's component draws (confirmed by playtest, 10 August); patch mesh in the
globe's component is black; patch mesh in its own component is black. Both components render the
globe's geometry and neither renders the patch's, so `PatchIntoGlobe`'s pivot was right: **the fault
is in the patch's mesh.**

Which leaves the sharpest statement of the problem yet, and it is a contradiction:

- The patch's mesh fails in two components that both draw the globe's.
- Its winding measures correct at runtime — 0 of 32768 inward, against the planet's centre.
- Reversing its indices makes it draw.

The only way all three hold is if the patch's triangles face away from the *viewer* while facing away
from the *planet's centre* — which happens if the viewer sits on the inside of the patch surface.
The HUD reports `0.00 km ground` while the patch is built from the same height function, so the
viewer is on the surface to within the precision of that readout, and which side of it is not
something any measurement so far has recorded.

**That candidate is dead, measured 10 August.** The report now logs the signed distance from the
camera to the patch surface directly beneath it — exact rather than sampled, since the patch's centre
vertex sits on the anchor at zero offset along the viewer's own direction. A headless descent gives
seven readings from 1671 m down to 49 m, at touchdown:

    camera is 1671.17 m ABOVE the patch surface beneath it (height function says 1671.17 m).
    camera is  912.30 m ABOVE ... ( 912.30 m).   camera is 504.78 m ABOVE ... (504.78 m).
    camera is  281.35 m ABOVE ... ( 281.35 m).   camera is 157.44 m ABOVE ... (157.44 m).
    camera is   88.30 m ABOVE ... (  88.30 m).   camera is  49.58 m ABOVE ... ( 49.58 m).

**The viewer is above its own ground at every altitude**, so the surface is not being seen from
beneath. The same line also confirms, at every reading, that **the mesh and the height function agree
to the centimetre** — the invariant the terrain model may never violate, unverified since the patch
moved onto the planet actor, and intact.

So the contradiction stands with nothing attached to it:

- The patch's mesh fails in two components that both draw the globe's.
- Its winding measures correct at runtime, 0 of 32768 inward.
- The viewer is above it, and it agrees with the height function exactly.
- Reversing its indices makes it draw.

Every explanation offered so far has been eliminated by measurement.

**The engine's render-buffer conversion is faithful, read 10 August**
(`MeshRenderBufferSet.h:504`, `InitializeBuffersFromOverlays`). Vertices are unshared, three per
triangle, written in the order `Tri[0], Tri[1], Tri[2]`; the index buffer is filled sequentially,
`IndexBuffer[idx] = idx`; no triangle is skipped, reordered or reoriented. **Winding in equals
winding out.** Both meshes take the same single-buffer path — neither has material IDs, so
`InitializeByMaterial` never runs, and `Mesh.ReverseCulling` depends only on the transform
determinant, which is positive for both. The conversion cannot be flipping one mesh and not the
other, so it is eliminated as the asymmetry.

**Two leads came out of that read, both about attributes rather than winding:**

1. Per-vertex normals silently substitute for the overlay. The loop reads
   `TriNormal[j] != InvalidID ? NormalOverlay->GetElement(...) : Mesh->GetVertexNormal(Tri[j])`,
   and neither builder ever sets per-vertex normals — only overlay elements. Any triangle whose
   overlay triple is unset gets whatever `GetVertexNormal` returns, and a zero normal feeds
   `MakePerpVectors` into a degenerate tangent basis. Both builders appear to set the overlay for
   every triangle; that appearance has not been measured.
2. **Neither mesh sets UVs at all.** `EnableAttributes()` creates a UV layer, the converter asks it
   for each triangle, gets `InvalidID`, and writes `(0,0)` to every vertex — a degenerate UV
   triangle, which is what auto-calculated tangents are built from.

**Both leads are dead, measured 10 August.** The builders now log attribute state, and a headless run
gives:

    globe: 55296 verts, 108300 tris, 0 missing a normal triple, 0 with UVs across 1 layer,
           55296 normal elements, per-vertex normals 0, tangent space 0, material ids 0
    patch: 16641 verts,  32768 tris, 0 missing a normal triple, 0 with UVs across 1 layer,
           16641 normal elements, per-vertex normals 0, tangent space 0, material ids 0

Normal elements equal vertex count in both, no triangle is missing a normal triple, so the
`GetVertexNormal` fallback never fires for either. Neither has UVs, tangent space or material IDs, so
both take precisely the same path through the converter.

**The two meshes are now identical in geometry, winding, attributes, component, transform and
conversion path, and one draws.** Every difference anyone has proposed has been measured and found
absent.

**The bisect ran, and the fault is not in `FPlanetPatch::Build`.** `SpaceMMO.GlobeCrop 4` takes the
globe — known-good geometry that draws — deletes every triangle outside a 4° cap around the viewer,
leaving 134 of 108300, and changes nothing whatever about how those vertices were generated. **It
disappears too**, confirmed by playtest on 10 August with the patch switched off.

So this was never about the patch generator. The trigger is something about an **open, patch-sized
cap on a `UDynamicMeshComponent`**, whatever produced its vertices — which also reframes
`PatchFlipWinding` as a workaround for a general phenomenon rather than a patch-specific quirk.

Note what that does *not* fit: the cropped cap keeps exactly the triangles that were visible in the
whole globe, sitting directly under the viewer, and removing everything else should not change what
is on screen. It does. Nothing currently explains that either.

The obvious follow-up, for whoever picks this up: reverse the cropped globe's winding as well. If a
reversed cap draws, the phenomenon is general — any open cap needs the opposite winding to a closed
sphere, and that is a single coherent statement about this engine that could be taken to a repro
outside this project.

So the standing contradiction is: two meshes, identically wound by measurement, through the same
component with the same determinant, and only one needs reversing. One of those statements must be
false, and finding which is the next job — `SpaceMMO.GlobeFlipWinding` was added on
10 August to A/B the globe the same way the patch was. The globe is otherwise built once at
BeginPlay, so Tick watches the value and rebuilds when it changes — without that the switch would
have set a value nothing ever read, which is exactly how the earlier experiments produced results
from runs that never happened. It logs both the change and the winding it built with. Untested. **Do not "fix" this by flipping the patch and
shipping it**; that would bake in a mystery and leave the tests asserting the opposite of what
renders.

Superseded reasoning, kept because it was wrong in an instructive way:

With `SpaceMMO.PatchFlipWinding 1` the ground appears, bright white under unlit. Reversing the index
order changes no position, so this is facing and nothing else. Combined with the tests, it measures
Unreal's convention directly: the patch's `cross(B-A, C-A)` provably points outward, and in that
orientation it is culled — therefore **a front face in Unreal has `cross(B-A, C-A)` pointing away
from the viewer**, and every surface in this project is built the opposite way.

The globe uses the identical index pattern and the identical outward orientation, so it is wound
backwards too. A closed sphere conceals this: with the near surface culled the viewer sees the inside
of the far hemisphere, which still reads as a planet. An open cap has no far side, so it disappears
entirely — and where relief tilts some faces the other way, those show, which is the white band at
the horizon and its disappearance when the terrain is flattened.

This likely also explains the long-running **black ground**: back faces shade against vertex normals
that point away from the viewer, so the surface lights as if lit from beneath. That is the
"blown-out white ore sitting on black ground" observation, and probably the reason the ambient and
exposure values were tuned the way they were.

Still to do: flip the globe as well, restore lighting, and check whether the black ground and the
lighting workarounds go with it. The winding tests should then assert the convention that actually
renders rather than "outward" — with the reasoning written down, because the geometric statement is
correct and the rendering one is its opposite, and that is exactly the sort of thing that gets
"fixed" back the wrong way later.

`SpaceMMO.PatchFlipWinding` was added the same day to test facing directly rather than by inference:
it swaps the second and third index as the mesh is appended, touching no position, forces a rebuild
when changed, and logs `Patch winding: reversed.` so it cannot silently not run. Untested as of
writing — ground appearing under it means the patch is rasterised back to front despite outward-wound
data; ground staying black eliminates facing and sends this to `MeshRenderBufferSetConverter`.

Note the correction: the winding assertion referred to above is `SpaceMMO.Patch.FrameIsRightHanded`,
not `FacesOutward` — the latter was cut as a duplicate of `SurvivesBeingWide`.

## 85 — Lost

No record survived. Left as a gap rather than invented.

## 86 — Let the patch carry the ground once it draws

**Done**, 10 August. `SpaceMMO.HideGlobeUnderPatch` now defaults to 1, confirmed by playtest: the
patch carries the ground and the globe hides beneath it, so the two samplings of one height function
no longer fight over the same space.

Still unexercised, and worth a look next time somebody is in the game anyway: the **release** path,
where the viewer leaves the atmosphere, the patch is dropped and the globe comes back. That code has
never run with a visible patch, so "the globe reappears" has never actually been observed. Reconstructed, not recovered: this is the task most likely to be the one
described as "tied into" the terrain work, but its original content is gone.

`SpaceMMO.HideGlobeUnderPatch` defaults to 0, so the globe draws the ground and the patch refines
nothing. The two are samplings of one height function, so drawing both puts the globe's coarse hills
through the patch's fine ones — only one may ever be visible.

Once the patch draws, default the flag to 1 and confirm the handover in both directions: descending
into the atmosphere (globe hidden) and leaving it (patch released, globe shown). The release path in
`UpdateTerrainPatch` has never been exercised with a visible patch.

## 87 — Instrument the live patch path

**Done**, 10 August. The report fires, confirmed by playtest and by every headless probe since — it
produced the proxy state, the bounds, the camera containment, the inward-facing counts, the attribute
comparison and the signed distance to the surface, which between them carried the whole of 84's
progress that day.

`ASpaceMMOTerrainPatchActor` was never spawned — a leftover from before patch building moved onto
`ASpaceMMOPlanetActor` — and it carried all the useful instrumentation: component triangle count,
material, visibility, registration, `SceneProxy`, bounds, camera distance. The live path logged one
line, which is why recent logs cannot say whether the patch drew.

Done: the instrumentation ported into `ASpaceMMOPlanetActor::ReportPatchIfPending()`, deferred one
frame because `MarkRenderStateDirty()` destroys the proxy and queues a replacement for end of frame;
`SpaceMMO.DirtyAfterMeshUpdate` removed; `SpaceMMO.RebuildGlobe`'s comment corrected where it
asserted the disproven hypothesis; the dead actor deleted. Builds clean, 153/153 automation tests
pass.

`SpaceMMO.Patch.FrameIsRightHanded` added, and verified to fail for the right reason: swapping the
cross products in `BuildTangentFrame` turns it red at all four directions, along with
`SurvivesBeingWide` (512 and 1152 triangles inward), `NormalsPointOutward` and `NormalsMatchTheGround`.
It started larger and was cut back — `SurvivesBeingWide` already counted inward faces at 4, 20, 45
and 60 degrees, so the triangle loop was a duplicate. What survives is the handedness assertion,
which nothing covered, including the pole branch where the reference axis switches from Z to X.

Remaining: a playtest confirming the report actually fires. If `Terrain patch at …` appears with no
indented lines beneath it, the diagnostic is not running and must be fixed before anything is read
into its silence.

## 88 — Give the terrain collision from the height function

**Done** — it already was, verified 11 August by reading rather than assumed. The task was written
from the `NoCollision` on both mesh components, which is the design working rather than a gap.

- `FPlanetTerrain::ResolveContact` is the collision, and it is a function of position, not of any
  mesh.
- `ASpaceMMOShipPawn::SimulateStep` calls it after advancing, and `SimulateStep` runs under
  `HasAuthority()` — so **the dedicated server resolves contact itself**, from the same height
  function the client tessellates. That is the whole point of ADR-0002 and it holds.
- `ASpaceMMOCharacterPawn` resolves the same way while walking.
- Deposits and stations are placed against the height function too, on every machine.

One thing worth knowing, and not a gap: **the C# backend has no height function at all** — no noise,
no elevation, nothing. It stores terrain configuration and serves it, and evaluation happens only in
C++, shared by the client and the UE dedicated server. So "the server agrees about where the ground
is" is true of the UE server and silent about the API. That is fine while nothing in the API needs an
elevation, and it stops being fine the moment one does — gathering range being the obvious candidate,
since it currently trusts a position the client computed.

## 89 — Caves need a second representation

**Decided, 11 August: [ADR-0011](adr/0011-caves-are-authored-volumes.md) — caves are authored
volumes, not a density field.** The implementation is not started. The scoping below is kept because
it is the reasoning the ADR was drawn from; the ADR itself is the decision.

Implementation, when it starts: a cave lookup in C++ (floor and ceiling within a volume) used by
deposit placement, contact and the mesher; cave definitions in `data/`; a `cave` reference on
deposits so a cave deposit takes its height from the cave rather than from an authored altitude;
and a carry-through field on the C# side that is never evaluated. See 96 — a cave is a shape, and
authoring shapes by hand in JSON is the part that will hurt.

A height field is a function of direction and cannot express an overhang, let alone a cave. Whatever
is added must keep the property that makes the current model work: the server and every client agree
about the ground without shipping any of it (ADR-0002). Three ways to do that, and they differ mostly
in where the difficulty is put.

**A. A density function — signed distance evaluated in both languages.** The natural extension: the
surface stops being a height and becomes a field, and caves fall out of it. Keeps every property of
ADR-0002. The difficulty is that C# and C++ must agree *bit for bit* on a noise function, which
ADR-0002 already names as the expensive thing to discover late — and today the backend has no noise
implementation at all, so that is a new shared component with a nasty failure mode.

**B. Authored cave volumes, as content.** A small number of hand-placed volumes in `data/`, read by
both sides, subtracted from the height field where they apply. No shared noise, no bit-exactness
problem, and caves become level design instead of mathematics. Loses procedural caves everywhere,
which nothing has asked for.

**C. Player-carved voxels, stored as deltas.** Fits ADR-0002's "database stores only player-caused
deltas" exactly, and is the largest build of the three.

**Recommendation: B first.** ADR-0007 has already made this trade once — it swapped a procedural
galaxy for one handcrafted system and removed an entire technical spine at a cost of a few hours of
authoring. The same reasoning applies here, and B does not foreclose A or C later, because the cave
lookup can be swapped underneath whatever reads it.

This is also the honest answer to "should we adopt a voxel plugin": that question is really this one,
and it is about what representation the server and client share rather than about which UE plugin
renders it. A plugin cannot run in the C# backend.

## 90 — Stop the landed state flapping

**Done**, 11 August, `7a206bb`. It was not a wrong state: every transition was honest arithmetic on a
rule with no memory.

Measured first. At 738 m/s and 60 Hz a ship crosses twelve metres of ground per frame, and this
terrain rises and falls by up to 0.33 m over that distance — more than the twenty centimetres that
decide contact, on 45 frames out of 60. A single threshold oscillates by construction.

`ResolveContact` now takes whether the caller was in contact last frame and releases on ten times the
band it captures on, which is the shape `ClassifyProximityAtAltitude` already uses. Deliberate
departures are unaffected because leaving is decided by separation speed, not distance. Both pawns
feed their previous state in; the character had the same fault while walking, less visibly.

`SpaceMMO.Terrain.ContactIsWiderToLeaveThanToArrive` was verified to fail with the multiplier set
back to 1, on the assertion that matters.

**The in-game confirmation ran on 11 August, and the premise was partly wrong.** The log still shows
66 transitions, some 13 ms apart — and they are honest.

**Orbital velocity on this planet is about 443 m/s**: `sqrt(9.81 m/s² × 20,000 m)`. The flight that
produced them was skimming at 650–870 m/s, which is 1.5 to 2 times orbital. At 868 m/s the
centripetal requirement is 37.7 m/s² against 9.81 available, so the ship is thrown off the ground by
its own speed and contact is genuinely intermittent. No contact rule should suppress that; doing so
would be lying about where the ship is.

At rest it is solid — 4.2 s quiet after the first landing, and nothing at all after the last one — so
the hysteresis is doing its job on the case it was built for, which is sub-metre threshold noise.

The lesson is about the diagnostic rather than the physics: honest skipping and a broken threshold
produced identical log lines, and the first response was to fix the threshold. The transition now
logs speed alongside orbital velocity so the two can never again be confused, and the reader is told
which one they are looking at.

**Worth deciding separately, and not a bug:** whether a ship at twice orbital velocity a few metres
above the ground should be possible at all. Decided, and fixed — see 98.

Note what actually closed this. The hysteresis was a real fix for a real defect and did nothing for
the symptom; **drag removed the symptom by making the condition unreachable**, since a ship can no
longer hold orbital speed in air. The lesson is the one the whole terrain investigation kept
teaching: the thing that looks broken and the thing that is broken are often two different things,
and only measurement tells them apart.

`ClientA.log`, 10 August, 15:05:33 — the ship alternates four times in 0.36 s while travelling about
600 m across the surface:

    15:05:33.237 Lifted off   (40.086, 3.584, -0.287) km
    15:05:33.253 Touched down (40.084, 3.575, -0.286) km
    15:05:33.516 Lifted off   (40.039, 3.390, -0.258) km
    15:05:33.520 Touched down (40.039, 3.387, -0.257) km

Two readings, and they want separating before anything is changed. It may be a missing hysteresis on
a threshold the ship is sitting exactly on — the same shape as the bug that had a landed ship
classified as flying because its altitude was measured from the sphere rather than the ground. Or it
may be honest: a ship genuinely skipping across terrain at speed, in which case the log is right and
only its noisiness is wrong.

Cheap to investigate without a playtest: the headless probe lands a ship on its own
(`-game -nullrhi -ShipStartX=38`), and the altitude and contact numbers can be logged either side of
the decision.

---

# M3 — closing the loop

Derived from the roadmap line in `README.md`, 11 August. The acceptance criterion for the whole
milestone is task 93: two players trading a player-made item. Everything else here exists to make
that possible or to prove it.

What is already built, verified by reading the code rather than assumed: `GatheringEndpoints`,
`IndustryEndpoints`, `MarketEndpoints`, `DockingEndpoints` and `QuestEndpoints` on the API;
`SpaceMMOGatheringComponent` and `SpaceMMODockingComponent` on the client; recipes covering
`refining`, `toolcrafting` and `shipcrafting` in `data/recipes/core.json`; and docking already
required before trading.

## 91 — Mine, as distinct from gathering

**Done**, 12 August, and the original premise was wrong. There is no missing verb.

Mining and gathering are the same action on different rocks. The client's
`SpaceMMOGatheringComponent` finds a deposit in range and sends its node id; the **server** decides
which skill is awarded from `node.SkillId`, and since the tool gate landed (94) it enforces the
difference too. A second component would duplicate the first and decide nothing.

What was actually missing is that a player cannot tell what a rock demands until it refuses them.
Half of that is now fixed: `ResourceNodeResponse` carries `requiredToolKey` and `requiredToolName`,
`FBackendResourceNode` parses them, and the protocol test uses the real response body — copied from
the running API, with a gated deposit and a bare-handed one, because a bare-handed deposit sends
`null` rather than omitting the field and a parser that read null as "some tool" would gate the very
deposit the onboarding chain starts with.

`BuildNearbyPanel` now says what the rock in front of you is, which skill works it, what level that
takes and where you are against it, and — since 100 landed and the client can see tools — which tool
it needs and whether you are carrying one.

It asks `USpaceMMOGatheringComponent::FindDepositInRange` rather than searching for itself, which is
why that method is now public: a panel with its own idea of "in reach" would eventually name one rock
while E worked another, and a player would be told they are standing at something they are not.

**A broken tool does not count as carried.** `GuardToolAsync` ignores condition zero, so a panel that
counted one would promise a gather the server then refuses — worse than saying nothing. Verified by
dropping the condition check and watching the test fail.

Pure and static like the other panels, so every case is testable without a running game: nothing in
reach, a tool carried, a tool missing, a tool broken, a level not yet reached, and silence about
level once it is. 161 client tests pass.

## 92 — Walk the loop once, on one machine

**Done** — validated by Joe, 11 August. The single-player loop runs end to end.

Gather → refine → craft → list → buy, by one player, start to finish, with the log showing each step
crediting the right inventory and awarding the right skill. Nobody has recorded doing this end to
end; the pieces have been built and tested separately, which is exactly the arrangement that hides a
seam.

Much of it can run headless — `-game -nullrhi` with the API up drives a client that gathers — so the
first pass need not cost a playtest. What it must not do is prove the loop with hand-built inputs on
either side of the wire; at least one step has to use what the other side actually sends.

## 93 — Two players trading a player-made item

**Done** — validated by Joe, 11 August. Two players have traded a player-made item, which is
the M3 acceptance criterion met.

Player A gathers, refines and crafts an item that did not exist before; docks; lists it. Player B
docks at the same station, buys it, and holds it. Both clients already exist as `ClientA` and
`ClientB` with separate logs and separate accounts, and the dedicated server flow is documented in
`scripts/host-dedicated.bat`.

Re-cook the server first — `check-staged-server.ps1` refuses to launch a stale one, and the staged
build was already three days behind the code on 10 August.

## 94 — Make the tool gate real

**Done**, 11 August. The inference in this task was wrong in an instructive way: the gate was fully
enforced and completely unreachable.

`GatheringService.GuardToolAsync` and `IndustryService.GuardToolAsync` both throw
`MissingToolException` when a character lacks a node's or recipe's required tool, both have had test
coverage all along, and the `required_tool_item_def_id` column has existed since the initial schema.
**`ResourceNodeContent` had no field for it**, so no authored deposit could ever set one, every
mining node was workable bare-handed, and every test still passed.

Fixed by adding `RequiredTool` to `ResourceNodeContent`, mapping it in `ContentLoader` the way a
recipe's tool is mapped, and gating all four mining deposits on `crude_mining_laser`. Gathering
deposits stay bare-handed, which the onboarding chain depends on — `craft_crude_mining_laser` takes
8 `scrap_alloy` and needs no tool itself, and its comment says why: "Deliberately needs no tool, or
the chain could never start."

The questline already taught the order — gather scrap, craft the tool, mine ore — so this is the
enforcement that makes its second step mean anything. Until now a player could skip the tool entirely.

**What the gate actually requires, checked on 11 August because it is easy to assume otherwise:**
possession, not equipment. `GuardToolAsync` asks for any instance of the tool that is not destroyed,
has `Condition > 0`, and sits in an inventory whose `CharacterId` is the character's. It does **not**
filter on `InventoryKind`, so a laser in a station hangar on the far side of the system satisfies it
while its owner mines bare-handed on a planet.

That is loose, and it cannot currently be tightened. Gathering and industry both deposit into a
station hangar via `GetOrCreateStationHangarAsync`, and `InventoryService` offers `Add`, `Remove`,
`QuantityOf` and `GetOrCreateStationHangar` — **no move, no transfer, and no endpoint for one**.
`CharacterCarried` is an enum value that nothing creates or fills. Restricting the check to items on
the character's person would therefore make mining impossible rather than stricter: the laser could
never get there. See 99.

Two tests, both verified to fail against the bug. One asserts a property of the shipped pack rather
than a count: every mining deposit requires a tool that exists, and no gathering deposit requires
one, so authoring more deposits cannot break it but violating the design rule will. The other checks
the count of gated nodes in the *database* against the pack after seeding, because the first proves
only that the JSON says the right thing — a mistyped line in the loader would leave it green while
every deposit stayed bare-handed.

## 95 — Award skills across the whole loop

**Done — it already was**, verified 11 August by reading rather than assumed. The inference in this
task was simply wrong.

Three call sites share one implementation: `GatheringService` awards `node.SkillId`,
`IndustryService` awards `recipe.SkillId` at **claim** rather than at start, and `QuestService`
awards on completion. `SkillAwards.AwardAsync` is deliberately the only implementation, because
three copies were once one bug written three times — a duplicate-key failure that appeared only when
two of them ran in the same unit of work.

And the content pays: all six shipped recipes carry a positive `xpPerRun`, covering `refining`,
`toolcrafting` and `shipcrafting`. Nothing was missing.

## 98 — Slow ships down in atmosphere

**Implemented 11 August; the number wants flying before it is settled.** Atmospheric drag, option A
below.

`FPlanetPhysics::AtmosphericDensity` is 1 at the ground and **exactly** 0 at the top of the
atmosphere and above — exactly, because an exponential tail would leave a whisper of drag acting in
orbit forever, and that is only ever noticed as an orbit decaying weeks later.
`FPlanetPhysics::AtmosphericDrag` is quadratic in speed and expressed through a terminal speed rather
than a coefficient, so the number a designer chooses says what it does: drag cancels full thrust at
`FShipFlightConfig::AtmosphericTerminalSpeed` at sea level.

Shipped at 20,000 cm/s — **200 m/s, or 400 boosted, against circular orbit at 443 m/s**. Under the
bar deliberately and not by much, and the test reads those values from the shipped config rather than
from constants, so raising the config past orbital turns it red. Verified by doing exactly that: at
300 m/s the boosted 600 fails on "Even boosted, a ship cannot hold orbital speed in the air".

It lives in the caller rather than in `FShipFlightModel::Step`, because `Step` already separates
pilot intent from the world acting on the ship and drag is unambiguously the world — which also keeps
flight assist damping only what the pilot does. Altitude is measured above the *ground* rather than
the sphere, so a ship in a valley is deeper in the air than one over a mountain.

**Done** — flown and confirmed by Joe, 11 August: 200 m/s feels right. The number stays.

That number was the only part a test could not settle, and it is now the one thing here backed by
nothing but a person flying it — which is the right kind of evidence for the question and worth
saying plainly, since everything around it is backed by measurement.

The 11 August flight skimmed the surface at 650–870 m/s, which on this planet is 1.5 to 2 times
orbital velocity — `sqrt(9.81 m/s² × 20,000 m)` is only about 443 m/s. Nothing stops that today: the
flight model is pure 6DOF with thrust in the ship's frame and velocity in the system frame, and no
force opposes motion at any altitude. A 20 km world makes orbital speed a very low bar to clear by
accident, and clearing it means the ground cannot be flown along at all, only skipped across.

Three ways, and the first is the one to want:

**A. Atmospheric drag.** A force opposing velocity, scaled by a density that falls off with altitude.
The planet already carries `AtmosphereHeightKilometres` (12 km on the capital) and proximity is
already classified by altitude, so the inputs exist. Gives a terminal velocity for free, makes
re-entry mean something, and costs the pilot nothing they would not expect. `FShipFlightModel::Step`
already takes gravity, so drag arrives the same way — and it is a pure function, so it is testable
headlessly to the same standard as the rest of the flight model.

**B. A speed cap inside the atmosphere.** One clamp, trivially implemented, and it will feel like
one: a ship pinned at a number regardless of thrust reads as a bug to anyone who has not seen the
code.

**C. Weaker thrust in atmosphere.** Does not solve it. A ship that is already fast stays fast, which
is exactly the case that produced this task.

Tuning is the real question rather than the mechanism: terminal velocity at sea level wants to be
comfortably below 443 m/s or the skimming comes straight back, and high enough that crossing a
continent is not tedious.

**Both ends must agree.** Flight is simulated under `HasAuthority()` on the dedicated server and
predicted on the client, so drag belongs in the shared model, not in either caller. Putting it
anywhere else would make the client and the server disagree about where a ship is, which is the
class of bug `ReceivedBunch: FieldCache == nullptr` taught this project to fear.

## 99 — Move items between inventories

**Server and API done**, 11 August. Raised from 94; the client affordance remains.

`InventoryService.TransferAsync` moves a stackable quantity and `TransferInstanceAsync` moves a
single instance — a different operation rather than a special case, because an instance carries its
own condition and acquisition value and so moves as itself rather than being split and recreated.

Three rules are enforced in the service rather than at any caller, so no endpoint can forget them:
both inventories must exist, they must belong to the same character, and a destroyed instance cannot
be recovered by moving it. Crossing owners is refused deliberately — giving goods away is a trade,
and a trade is the market's job, with fees, an order book and a settlement path that a silent
transfer would route around.

**Cost basis travels with the goods**, which is the part that would have been silently wrong.
Insurance pays against acquisition value (ADR-0006), so material arriving as though it had cost
nothing is material that pays nothing when lost. `RemoveAsync` already returns the share that left
and the destination is handed exactly that, so a stack split across two containers still sums to what
it originally cost. Verified by handing `Credits.Zero` across instead and watching the test fail.

Volume is deliberately not checked. `CapacityM3` exists and hangars are created at 0, nothing
anywhere enforces it, and a transfer is the wrong place to invent that rule — it would apply to one
route into a hold and not to the others.

**Presence is enforced at the endpoint**, which answers the question this task left open. Goods do
not move in or out of a station hangar unless the character is docked at that station — the same rule
the market runs on, and for the same reason. Without it, hauling planet-locked materials (ADR-0008)
would be a request rather than a flight, and four worlds would collapse into one warehouse with no
error and no failing request. Ship holds and carried items travel with their owner, so only hangars
are checked.

`POST /characters/{id}/inventory/transfer` and `.../transfer-instance` — two routes rather than one
switching on which field was populated, because they are two operations. Ownership stays in the
service so no endpoint can forget it; the endpoints add only presence.

Five API tests, including the half-rule case: loading out while docked, undocking, and being refused
on the way back in. Verified by deleting the presence check and watching the three refusal tests go
red. 636 backend tests pass.

Remaining: a client affordance, and volume limits whenever `CapacityM3` starts meaning something.

Everything a character gathers or crafts lands in a **station hangar**, and nothing can leave it.
`InventoryService` has `Add`, `Remove`, `QuantityOf` and `GetOrCreateStationHangar`; there is no
move, no transfer, and no endpoint exposing one. `InventoryKind.CharacterCarried` and
`InventoryKind.ShipHold` both exist and are both documented in the enum — carried items go into
on-foot combat and death resolution, a ship's hold is "the thing that is inside the explosion when a
ship is destroyed" — but nothing routes anything into either.

This blocks more than it looks:

- **Equipping tools.** There is no point in an equip slot while the only inventory that can hold
  anything is a building. 94's tool gate is possession-anywhere for exactly this reason.
- **Hauling.** Four materials are planet-locked by ADR-0008 and the cross-faction recipe needs all
  four, which is the fact the whole trade economy rests on. If goods cannot enter a ship's hold, the
  only way materials cross the system is the market — and that is a different game from flying them.
- **Death and insurance.** ADR-0006 destroys loot by cause, and both enum comments describe what is
  lost where. Neither rule can bite while everything lives in a hangar.

Worth deciding before building: whether a transfer is free at a station and impossible elsewhere,
whether volume limits apply on the way in, and whether the client needs a real inventory screen or
whether the existing panel can carry it.

## 100 — Show players the things they own that do not stack

**Done**, 11 August. Found while reframing 91, and worse than what it was found during.

`GET /characters/{id}/inventory` read `InventoryItems`, which is stacks only, while every category
carrying condition — tools, modules, armour, weapons, hulls — lives as an `ItemInstance`. So a player
could follow the questline, craft a Crude Mining Laser, and see nothing anywhere: owned, usable, and
absent from their own inventory, which reads exactly like a craft that silently failed. That is the
entire reward of quest step two.

The response is now an envelope with `stacks` and `items` rather than a bare array. One response
rather than a second endpoint, deliberately: a client that fetched only stacks would show half a
player's possessions and believe it was finished, which is the bug being fixed one layer up. The two
lists use the same field names for the same ideas (`name`, `kind`) so nobody has to remember which
half they are reading.

Instances are kept apart from stacks rather than folded in with nullable fields, because two lasers
at different condition are two things and a quantity of 2 says they are one — and ADR-0006 insures
each instance against its own acquisition value, so the distinction is load-bearing.

Destroyed instances are excluded. Their rows survive so history does, and listing them would show a
player the wreck of something they lost and let them conclude they still had it.

Client side: `FBackendItemInstance`, a parser that tolerates a stacks-only response from an older
server (no tools rather than no inventory, the milder failure), and a Holdings panel that lists each
one with its condition, sorted by name then condition so two of the same tool can be told apart.

159 client tests and 637 backend tests pass. The API test asserts the laser appears with its
condition and that a destroyed one does not; the protocol test parses the envelope with one of each.

---

# M5 — an interface

Added 12 August. **There is no UMG or Slate anywhere in `client/`.** Every screen in the game is
`GEngine->AddOnScreenDebugMessage`, including the character panel, the market, industry, quests and
every notice. That was a reasonable way to get a vertical slice moving and it has now run out of
road twice in one day: a gather message vanished because debug messages are ordered by slot rather
than by key and the panel is redrawn every frame, and fixing it cost the message its colour, because
the panel had to become a single entry to have a stable order at all.

Two design documents already assume a UI that does not exist — `design-bible.md` §3 on item
categories that "drive UI grouping", and `economy-design.md` §7 on price history as "the price-history
source for market UI".

Combat (M6) sits behind this deliberately. Health, targeting, damage and being shot at cannot be
reported by text the engine reorders every frame.

**Nothing in this milestone gets built before Joe has seen it and said yes.** Every widget, screen,
layout and HUD arrangement is presented first — a sketch, a mock, a described layout, whatever
carries the idea — and confirmed or corrected before any of it is implemented.

**There are two unrelated binding systems in a Widget Blueprint, and they produce similar-looking
errors.** Learned 12 August, at the cost of two wrong answers.

- The **Details panel's `Bind` dropdown** is the classic UMG property binding
  (`FDelegateEditorBinding`, validated in `UMGEditor/Private/WidgetBlueprint.cpp:447`). This is the
  one this project uses.
- The **View Bindings panel** — a button in the widget editor's bottom status bar, or
  `Window → View Bindings` — belongs to the **Model-View-ViewModel plugin**, which is enabled and
  which nothing here uses. `+ Add Binding` there stamps out a row pre-filled with the selected
  widget as destination and no source, and that alone fails compilation with
  `Binding 'X <- <none>': A source path is required, but not set.`
  (`Plugins/Runtime/ModelViewViewModel/.../MVVMViewBlueprintCompiler.cpp:1147`).

The MVVM row lives in the blueprint's view extension keyed by widget *name*, so **deleting the
widget does not remove it** — re-adding a widget with the same name re-attaches the same broken row,
which is what makes it look unfixable. Delete the row in the View Bindings panel.

Worth grepping the engine for any UMG compiler message before theorising: the string above exists in
exactly one file, and it named the plugin responsible in one search.

This is not ceremony. Interface is taste, and taste cannot be derived from a specification: a panel
can satisfy every requirement written down and still be the wrong shape to look at, and the way that
surfaces is a day of work that has to be argued about instead of thrown away. Everything else in this
repository can be checked against a test or a log; this cannot, so it gets checked against the person
who has to look at it.

Applies to changes as much as to new screens. Moving something, re-ordering it, or deciding what a
row says is the same decision at a smaller scale.

## 106 — A real HUD

**In progress. Layout agreed with Joe, 12 August — Option A, contextual. The flying readout is
built and confirmed on screen; the other three contexts are not started.**

Done so far:

- `SpaceMMOBackend.Build.cs` depends on `UMG`.
- `USpaceMMOFlightReadout` words the readout in C++ as a pure static `Build()`, tested headlessly
  (`SpaceMMOFlightReadoutTests.cpp`) for the cases a screenshot cannot settle: metres versus
  kilometres either side of 1 km, ground altitude rather than sphere altitude in the pilot's line,
  and an omitted orbital speed where there is nothing to orbit.
- `WBP_FlightReadout` owns the layout, and **every label is the Blueprint's** — C++ emits bare
  values. This is the whole point of the split: Joe re-anchored the readout from top-left to
  top-right without a rebuild.
- `USpaceMMOHudSettings` names the Blueprint in `DefaultGame.ini` rather than hard-coding a path.
  Soft and unset by default, so the automated runs — which have no viewport — get no HUD rather
  than a failed load.
- The three `AddOnScreenDebugMessage` flight readouts it replaced are gone.

**A widget may not set its own visibility, and this is the second engine rule this task has run
into.** Found by playtest 12 August: the readout vanished on leaving the ship, as intended, and
never returned on getting back in.

`SWidget::Paint` is what calls `SWidget::Tick` (`SWidget.cpp:1505`), and a compound widget arranges
its children through an `EVisibility::Visible` filter (`SCompoundWidget.cpp:24`) — so a collapsed or
hidden widget is not painted, and a widget that is not painted does not tick. `USpaceMMOFlightReadout`
collapsed itself from `NativeTick`, which stopped the very tick that would have shown it again. A
one-way door.

`ASpaceMMOPlayerController::UpdateHudContext` owns the decision now, from an actor tick that runs
whatever is on screen. **Every context widget in this milestone must be shown and hidden from there**,
never from its own tick — which suits the contextual design anyway, since the controller is the thing
that knows which pawn the player is in.

Note what did *not* catch this: 164 green client tests, three of them on this widget. They cover
`Build()`, which was never wrong. Nothing headless can see a Slate visibility loop.

**Top-left is the engine's, so do not put a widget there.** `UnrealEngine.cpp:13619` fixes
on-screen debug messages at `x=40, y=45` in the editor (`100` in game) running downward, hardcoded
with no console variable. The flight readout was originally placed there and sat underneath the
character panel. It now lives top-right, confirmed by playtest 12 August. Every remaining widget in
this milestone has the same constraint until the debug-text panel itself is gone.

**The on-foot context is done — built, wired and QA'd by Joe, 13 August.** Three widgets, agreed as
sketches first:

- **`USpaceMMOOnFootReadout`** — name and credits, top right. Shares the flight readout's corner
  because the two are never on screen together.
- **`USpaceMMODepositPrompt`** — above the reticle, since it describes what you are looking at
  rather than what you are. Collapses entirely with nothing in reach. Carries the blockers as
  separate fields so the Blueprint can colour "you are lv 1" and "you have none" without colouring
  the rest — which is the whole reason this is a widget: as debug text those lines carried the same
  weight as everything else.
- **`USpaceMMOSkillsScreen`** + `USpaceMMOSkillRow` — every skill, opened with **K** (verified free
  against `DefaultInput.ini`). Trained first, alphabetical within each group.

Three decisions worth keeping:

1. **The key hint reads the actual binding.** `FindGatherKey` asks `UInputSettings` what is bound to
   `Gather` rather than the Blueprint printing "E". A hint that says E after somebody rebinds is
   confidently wrong and the player cannot tell.
2. **Every skill, not just trained ones** — a reversal of `BuildCharacterPanel`'s `Xp > 0` filter,
   which is right for an always-on panel and wrong for a screen somebody opens on purpose. M6's
   eight combat skills would otherwise be undiscoverable.
3. **Untrained skills read `lv 1`, not a dash.** The sketch said a dash; the skills endpoint says
   every character has every skill at level 1 from creation, so a dash would claim something the
   server does not.

**Progress figures are served, not derived.** `SkillResponse` gained `xpToNextLevel` and
`progressToNextLevel` from `SkillCurve.Describe`. A C++ copy of the curve would have to reproduce
`BuildThresholds`' order-sensitive flooring — its own comment says doing the division and the
accumulation the other way round changes some levels — or disagree silently. Same reasoning ADR-0002
applies to the height function. The client keeps a **negative sentinel** for "not sent", because zero
means "just started this level" and an older server would otherwise make every skill look freshly
begun.

**A Scale Box swallowed the deposit prompt, and the diagnosis is worth keeping.** The prompt never
appeared. Rather than guess, the tick was instrumented to log which of four things had gone wrong —
no pawn, no gathering component, nothing in range, or a node with no key — plus the widget's own
resolved visibility, and to say so on change rather than per frame. One playtest answered it:

    Deposit prompt: deposit found. bHasDeposit 1, item 'Ferrite Ore', own visibility 3.

Every flag the Blueprint binds against was correct and the widget was on screen (3 is
`HitTestInvisible`), which put the fault inside the Widget Blueprint and nowhere else. It was a
**Scale Box between the Canvas Panel and the content**; removing it fixed it. A Scale Box scales its
child to its own slot, so a canvas slot without a real size scales the content to nothing — no
error, no warning, and identical from the outside to a binding that never fires.

The instrumentation was removed once it had answered. What it proves is the approach: **silence from
that log would itself have been the answer**, meaning the tick was not running at all, and it was
written that way on purpose so a negative could not be ambiguous.

Also worth knowing, checked while diagnosing: `.uasset` files are compressed, so `strings` finds
nothing in them. A Widget Blueprint cannot be inspected from outside the editor.

**Once the debug text is gone, Joe moves the flight and on-foot blocks to top left** — his call,
13 August, done in the Blueprints without a rebuild. Recorded here because it only becomes
actionable after the docked overlay lands and removes the last of the on-screen messages.

**Transient messages are done — built, wired and QA'd by Joe, 13 August.**
`USpaceMMOTransientMessages` + `USpaceMMOTransientMessageRow`, floating above the pawn.

This is the piece the whole milestone was created for. `ShowTransientLine` had to be folded into the
debug panel to get a stable order at all, and the price was colour; the widget gets it back through
`ESpaceMMOMessageTone`, which says whether something was gained without saying what colour that is.

Three decisions:

1. **First person is handled explicitly.** Both pawns have a `FirstPersonCamera` on `C`, and in first
   person the camera is inside the pawn — there is nothing to float above, and the projection lands
   behind the viewer. `FollowPawn` falls back to a fixed spot under the reticle whenever
   `ProjectWorldLocationToWidgetPosition` fails, so a message can never be silently off screen.
2. **Three messages, not one.** Mining produces them faster than they can be read, and a yield
   followed immediately by "give it a moment" would erase the yield being looked for. Oldest out
   first, each on its own expiry.
3. **C++ owns when a message goes; the Blueprint owns how.** `OnMessageAdded` is a
   `BlueprintImplementableEvent` so a Widget Animation can fade or rise it.

Two details worth keeping. The message is offset along the **pawn's** up vector, not the world's —
on a sphere those differ everywhere but one point, and the world's would put it sideways — and by a
multiple of the pawn's own bounding radius, so a ship and a character both clear themselves with no
per-pawn tuning. And `ESpaceMMOMessageTone` lives in `SpaceMMOBackendTypes.h` rather than beside the
widget, because the player controller's public header names it and that header must not drag UMG in:
UMG is a private dependency of this module.

**Unset config is a supported state**, and degrades rather than fails: with no Widget Blueprint
configured, `ShowTransientMessage` falls back to `ShowTransientLine`. No colour and no position, but
visible — and the gather result is the only feedback a key press gives.

**The deposit prompt floats over the rock, not the reticle** — Joe, 13 August. It describes the
thing it names, so it goes on the thing it names, and it follows as you circle it.

The projection is shared with the transient messages in `SpaceMMOHudPlacement.h` rather than written
twice, because both answer "where on screen is the top of that actor" and both get it wrong the same
two ways separately: **up is the actor's up, not the world's** (on a sphere those agree at exactly
one point), and the height is a multiple of the actor's own bounding radius, so a rock, a character
and a ship all clear themselves untuned.

**A camera adds a 10 m box to its actor's bounds, and that cost three playtests.** After the fix
below, the deposit prompt landed correctly and the transient messages disappeared entirely. The
messages were being placed at `(960, -8238)` on a 1921x1080 viewport — horizontally perfect,
**8,238 pixels above the top of the screen**.

Three explanations were proposed and all three were wrong. What ended it was enumerating the pawn's
bounding-box contributors rather than proposing a fourth:

    Hull (StaticMeshComponent):   radius  122 cm
    CameraProxyMeshComponent_0:   radius   57 cm
    DrawFrustumComponent_0:       radius 1010 cm
    CameraProxyMeshComponent_1:   radius   57 cm
    DrawFrustumComponent_1:       radius 1010 cm

**Every `UCameraComponent` registers a `DrawFrustumComponent` and a `CameraProxyMeshComponent`
outside shipping builds.** Both pawns carry two cameras — first and third person — so four
visualisation primitives swamped a 0.4 x 0.9 m character and put its bounding radius at 19 m. The
label was faithfully placed 19 m above it. Deposits have no camera, which is why the same code
worked there and made the fault look widget-specific.

`SpaceMMO::Hud::VisibleBounds` now gathers the box itself, skipping anything `IsEditorOnly()`.

**Not `IsVisualizationComponent()`, which is the obvious name and does not compile for the dedicated
server** — it and its flag live inside `WITH_EDITORONLY_DATA` (`ActorComponent.h:346`), so the cook
failed after the editor build had been green for an hour. `IsEditorOnly()` is unguarded
(`ActorComponent.h:708`) and answers the same question, because `SetIsVisualizationComponent` sets
`bIsEditorOnly` too. This is the second time the server target has caught client-only code, which is
exactly what `docs/setup.md` keeps it around for — and the lesson is to **build `SpaceMMOServer`
before starting a cook**, since it takes two minutes against the cook's twenty. Measured through the shipping path afterwards: **visible radius 122 cm
against a whole-actor radius of 2242 cm.**

Two things worth carrying forward. **`GetActorBounds` is unusable for anything a player looks at** —
it cannot include non-colliding components and exclude visualisation ones at the same time, and both
are needed here. And **the probe belongs in the controller, not the widget**: a widget only ticks
while it is painted, so the first attempt at this diagnostic logged nothing at all in a headless
run, and moving it to the controller made the whole question answerable with `-game -nullrhi` at no
cost to anybody.

**Bounds must include non-colliding components, and this cost a playtest.** `GetActorBounds`'s
first argument is `bOnlyCollidingComponents`, and it was passed `true`. A deposit's marker mesh is
deliberately `NoCollision` (`SpaceMMODepositActor.cpp:40`), so no component qualified, and
`GetComponentsBoundingBox` returns an `FBox(ForceInit)` that nothing expands — a zero box at the
world origin (`Actor.cpp:2267`) rather than a failure. The prompt was projected at the origin, which
under render-origin rebasing lands somewhere arbitrary and usually still on screen, so it read as a
label with a mysterious offset rather than one pointing at nothing. The pawn has a collision capsule,
which is why the transient messages were unaffected and the fault looked deposit-specific.

A label goes over what a player can *see*, so it wants visual bounds, not collision bounds.

The two differ deliberately in what they do when the projection fails. **Messages fall back** to a
fixed spot under the reticle — they must be seen, and first person puts the camera inside the pawn.
**The prompt hides** — `FindDepositInRange` returns the nearest deposit in reach, not the one being
looked at, so it can be behind the player, and a label pinned at the screen edge naming something out
of view describes nothing. `bHasDeposit` therefore means "in reach *and* on screen".

**Three of the four contexts are done and QA'd**: flying, on foot, and transient messages in both.
The debug-text panel still draws Holdings, Quests, Market, Industry and Jobs top-left, and goes when
the docked overlay lands — at which point Joe moves the flight and on-foot blocks to top left.

**The docked overlay is done — built, wired and QA'd by Joe, 14 August.**
`USpaceMMOStationOverlay` + `USpaceMMOTextRow`, tabbed: Market, Industry, Quests.

- **Opens on docking and toggles with `Tab`**; does nothing when undocked, because the overlay is
  about a place and a refusal on every stray keypress gets old faster than it informs. Undocking
  closes it rather than leaving a station's market floating over open space.
- **Tabs on `1`, `2`, `3`**, and only while it is open, so those keys stay free and cannot change
  something the player cannot see.
- **It renders the existing panel builders' lines** rather than restructuring them.
  `BuildMarketPanel`, `BuildIndustryPanel` and `BuildQuestPanel` are the only automated coverage the
  HUD's wording has, and the market half is about to be rewritten by 109 anyway — restructuring it
  twice would be waste.
- **`Visible`, not `HitTestInvisible`**, unlike every other HUD widget: this is the one meant to be
  interacted with once it grows mouse support.

**`Tab` is shared with the debug panel on purpose, and that is scaffolding.** The overlay takes it
while docked; the debug panel keeps it otherwise, because that panel is the only way to reach
Holdings until 108's inventory overlay lands. When 108 ships, the guard in `ToggleCharacterPanel`
goes and `Tab` becomes the station overlay outright. This deviates from the agreed "does nothing when
undocked" — it does nothing *for the overlay*, and the old panel still answers, which is the lesser
evil against making Holdings unreachable.

**106 is complete.** All four contexts are built and QA'd: flying, on foot, transient messages in
both, and the docked station screen — plus skills on `K`. What remains on screen of the old debug
text is Holdings, which 108's `I` overlay takes; when that lands the panel goes entirely, the guard
in `ToggleCharacterPanel` goes with it, `Tab` becomes the station overlay outright, and Joe moves the
flight and on-foot blocks to top left.

The panel's own comments are the specification for why: on-screen messages are ordered by slot
rather than key, are deleted and re-added every frame at zero display time, and so land in an order
nothing can influence — "the ship's own readouts use keys 1, 3 and 2 and render as 2, 3, 1". A widget
has none of those problems, and gets back the colour that `ShowTransientLine` had to give up.

**Keep the panel builders.** `BuildCharacterPanel`, `BuildMarketPanel`, `BuildIndustryPanel`,
`BuildQuestPanel` and `BuildNearbyPanel` are pure static functions with headless tests, which is why
the HUD's logic is testable at all. A widget should render their output rather than replace them, or
that coverage goes with them.

### Agreed shape

**Contextual rather than everything at once.** Today one always-on block shows the market book in
deep space and industry recipes mid-flight.

- **Flying** — a compact nav readout: system position, speed against orbital speed, altitude,
  proximity band, rebase count.
- **On foot** — name and credits, and the nearby deposit with its skill, level and tool.
- **Docked** — a station screen as an **overlay**, opened deliberately rather than always on, with
  Market, Industry and Quests. Overlay rather than full-screen, so the world stays visible behind it.

  **Holdings was listed here and has since moved out**, to its own `I` overlay — see 108, which
  records why. What remains is the station's own services, which are place-bound by ADR-0008.

**Transient messages float above the character pawn** rather than sitting in a corner — a
world-space widget on the pawn, so a yield or a refusal appears where the player is looking.

While flying they float **above the ship**, for the same reason — agreed 12 August.

**Editor-tweakable by design.** C++ owns the data through a `UUserWidget` subclass with
`meta = (BindWidget)`; Widget Blueprints own layout, fonts, colours and anchoring, and can be edited
without a rebuild. A missing `BindWidget` name fails Blueprint compilation with a clear error, so the
contract cannot drift silently.

`.uasset` in the repository is accepted for this, and is not new: `client/Content` already holds 726
of them — the third-person template, its animation library, and the deposit meshes.

**Nothing here gets built before Joe has seen it**, per the rule at the top of this milestone. The
layout above is agreed; the individual widgets are not yet.

## 107 — Sign in from the game

**Pending.**

There is no login screen. Credentials are read from `secrets/player-login.txt`, and `play.bat`
explains why: command lines mangle values here, and `-BackendEmail=someone@gmail.com` has arrived as
`someone@gmail .com`, producing a 401 that looks exactly like a wrong password.

That is a fine developer affordance and not something a player can be asked to do. Needs a screen, and
a decision about where a token lives between sessions — `USpaceMMOBackendClient` deliberately does
not persist one today.

## 108 — Inventory and transfer screen

**Pending. Decided 13 August: its own overlay, opened with `I`** (verified free against
`DefaultInput.ini`), rather than a section inside the docked overlay.

**This changes what the docked overlay is, and the change is an improvement.** Two keyed overlays
now exist — `K` for skills, `I` for inventory — and they have something in common that the rest of
106's docked screen does not: they are about *you*, they mean the same thing everywhere, and there
is no reason to make somebody land to read them. What is left for the docked overlay is Market,
Industry and Quests, which are about *the station* and are place-bound by ADR-0008 anyway.

So the split is now:

- **Keyed, always available** — Skills (`K`), Inventory (`I`). About the character.
- **Docked overlay** — Market, Industry, Quests. About the station, and reachable only there.

Holdings therefore leaves the docked overlay entirely; 106's agreed shape listed it there and this
supersedes that.

**Written 14 August, awaiting its Blueprints.** `USpaceMMOInventoryScreen` +
`USpaceMMOInventoryRow`, opened with `I` — which works in flight as well as on foot, since knowing
what is still sitting in a hangar is most useful while deciding where to fly. Opening refreshes from
the backend rather than polling: a screen opened to work out where something went is the worst
moment to be showing a stale copy.

Order is carried, ship hold, **the hangar being stood in**, then everywhere else alphabetically —
what is to hand at the top, which is the order the questions get asked in. Reachability mirrors what
the API enforces rather than guessing at it: a hangar counts as reachable only when docked there,
which is the same rule `RefuseIfNotPresentAsync` applies to a transfer.

A hangar whose station has not been fetched is named by id (`HANGAR — 41`) rather than left with an
empty heading, and an empty inventory says "Nothing yet" rather than rendering a blank rectangle
that reads as a screen which failed to load.

**Read-only first, decided 13 August.** Moving goods needs a selection model and a quantity
affordance and roughly doubles the work; seeing what you own and where is a real screen on its own,
and it is the half that is missing today.

**Transfer, when it lands, is drag between groups** — Joe's preference, 13 August. More work than a
keyed affordance, and worth it.

**Distant hangars are listed and greyed, not hidden.** The API refuses transfers when the character
is not docked there (`RefuseIfNotPresentAsync`), so they are genuinely unusable from elsewhere — but
knowing that 1,480 ferrite is sitting two planets away is exactly the thing a hauling game wants a
player to feel, and hiding it would make the goods simply vanish.

Grouped by container, because that is the fact the screen exists to convey. `InventoryItemResponse`
already carries `kind` and `stationId`, so the grouping needs no API change. Stacks show a quantity
and instances show condition, kept apart for the reason ADR-0006 makes load-bearing: two lasers at
different condition are two things, and a quantity of 2 says they are one.

Blocks the useful half of 99.

Moving goods between a hangar and a ship hold works over HTTP and has no way to be asked for in game.
Hauling is M4's premise and is currently reachable only with curl.

Wants: what is in each container, drag or a pick-quantity affordance, and the refusals surfaced —
`not_docked` and `insufficient_items` already come back with reasons a screen could act on.

## 109 — Market screen

**Pending.** Supersedes the workaround half of 105.

Today the book can only be seen for an item the player already holds, because the fetch is keyed to
their selected holding. A market screen at a station should show what is for sale regardless of what
the viewer owns, which is the whole point of a market.

`economy-design.md` §7 already names price history as the source for this.

## 110 — Menus

**Pending.**

No main menu, no character select, no settings. A character is chosen today with `-CharacterId=10`
on a command line, or by taking the first one on the account.

## 105 — You can only see the book for something you already own

**Decided 13 August: a station's market screen shows everything for sale there, and has a search.**
Both halves, not one or the other — browsing answers "what is there?", search answers "who has the
thing I came for?", and a market with only one of them fails whichever question the player actually
arrived with.

That rules out the third option below (listing only what the station has asks for) as the whole
answer, though it remains what browsing shows when nothing is searched.

Implementation is part of 109. Found by playtest, 12 August.

Player A listed ferrite ore; player B saw `asks: none  bids: none` until B docked at the trading hub
and cycled their holdings, at which point it appeared.

The station half of that is by design and worth keeping — a market is a place, and ADR-0008 makes
being at it what entitles you to use it. The **item** half is not designed, it is a side effect:
`RefreshBook` fetches for `(DockedStationId(), Selected.ItemDefId)`, and the selection comes from
`TryGetSelectedHolding` — the player's own inventory. So the only books reachable are for things
already held.

That is backwards for a buyer. Somebody who wants ferrite and has none cannot see that any is for
sale, cannot see the price, and has no way to discover the market exists for that item — in a game
whose entire premise is that every tradeable good was made by another player.

Not urgent, and deliberately not fixed on the spot: it needs a decision about what a station's market
screen actually is. Browsing the whole book at a station is one answer; searching by item is another;
listing what the station has any asks for at all is a third and probably the smallest.

---

# M6 — combat

Added 12 August, from `docs/design-bible.md` §2 and ADRs 0006, 0008 and 0009. **The roadmap had no
combat milestone at all**, while the design bible defines eight combat and pilot skills, explicitly
defers `constitution` and `stamina` XP "to the combat milestone", and three accepted ADRs describe
who may shoot whom, what a security zone means and what dying costs. Rules were decided; nothing to
shoot with was ever scheduled.

Nothing here is started. The tasks below are derived from those documents rather than recovered from
any list, and each says which document it comes from so the next reader can check rather than trust.

## 101 — Seed the combat and pilot skills

**Pending.**

`data/skills/core.json` holds five skills — gathering, mining, refining, toolcrafting, shipcrafting
— and the design bible §2 defines eight more that do not exist anywhere: `guns`, `melee`,
`constitution`, `stamina`, `ship_handling`, `lasers`, `missiles`, `warp`. Also missing from the
crafting tree: `armorcrafting`, `weaponcrafting`, `electronics`, `construction`.

Cheap in itself — the skill system, XP curve and awarding all work — but blocked behind 102 for the
two that have no XP source, and pointless before there is an action that awards the rest.

## 102 — Decide where `constitution` and `stamina` XP comes from

**Pending. A decision, not an implementation.**

Design bible §2 names this as open: they "are pools, not activities, so they need a defined XP source
before they're implemented — most likely awarded passively alongside combat actions, as RuneScape
does with Hitpoints."

That parenthetical is a proposal, not a decision, and it has consequences worth choosing
deliberately: passive award ties health progression to whichever combat skill a player uses, which
means a pure-melee character and a pure-guns character level constitution at different rates unless
the rate is normalised. Worth settling before 101 seeds the rows, because changing an XP source after
players have XP is a migration and an apology.

## 103 — Make death and insurance real

**Pending.** ADR-0006 is accepted and entirely inert.

Cause-based loot destruction and acquisition-value insurance are implemented in `SpaceMMO.Domain`
and covered by tests, and nothing can currently die. The rules also cannot bite while everything a
player owns lives in a station hangar — the enum comments describe what is lost from a ship's hold
and what is carried into on-foot combat, and neither container is reachable until 99 lands a client
affordance.

So this is blocked on M4 rather than on combat: the interesting half of dying is what it costs, and
that requires goods to be somewhere they can be lost.

## 104 — Pairwise aggression and security zones

**Pending.** ADR-0008 and ADR-0009 are accepted and unimplemented.

ADR-0009 settles the rule — aggression is pairwise, faction space is defended rather than hunted —
and ADR-0008 sets what a security zone means for who may attack whom. `SecurityLevel` already exists
on systems and bodies and is seeded, so the data is there and nothing reads it for anything but
display.

Worth doing early in the milestone rather than late: it decides where combat may happen at all, and
building weapons first would mean tuning them in a world with no rules about where they may be
fired.

## 111 — Gathering and industry ignore where you are

**Pending. Found by Joe, 14 August**, refining at DeepDock and watching the output appear at the
capital.

`ASpaceMMOPlayerController::StationId` is a hardcoded `1` — its own comment says "Not yet chosen per
player" — and it is sent as the storage station for gathering *and* as the station for `StartJob`.
`IndustryService` then uses that one station for both halves: it consumes inputs from
`GetOrCreateStationHangarAsync(characterId, stationId)` and deposits outputs back into it. So a job
started anywhere in the system is really run at station 1, which is self-consistent and therefore
invisible until somebody docks elsewhere and looks.

The fix is to send `DockedStationId()`. **It must not land before a client can move goods**, because
on its own it makes crafting possible only where the materials already happen to be, with no in-game
remedy — transfer exists over HTTP (99) and the inventory screen shipped read-only (108). Order:
transfer affordance, then this.

Blocked on 108's transfer half.

## 112 — Goods are held, not teleported

**Pending. Joe's stated direction, 14 August.** A design decision rather than a wiring fix, recorded
before anything is built.

Today everything gathered or crafted appears in a station hangar wherever the player is, which is
what 111 is about at the mechanical level — but the deeper point is that **goods never travel**. What
Joe wants instead:

- Gathering and crafting deposit into **carried** inventory (or a ship's hold).
- Moving goods to a station is a deliberate transfer, not automatic.
- Crafting and refining, while docked, may draw on **both** carried and station inventory.

This is the direction 99 already pointed at without spelling out: `CharacterCarried` and `ShipHold`
both exist, are both documented in the enum, and nothing routes anything into either.

**It needs `CapacityM3` to start meaning something.** It exists, hangars are created at 0, and
nothing anywhere enforces it (99). Without a volume limit, "held" is an infinite backpack and the
change buys nothing — capacity is precisely what turns ADR-0008's planet-locked materials into
flights rather than paperwork.

### Settled by Joe, 14 August

**A character on foot carries goods, limited by weight.** Default 50 kg, raised by the `stamina`
skill, and further by a backpack — an equippable, so M4's "equippable tools, weapons and armour".

**Death: a few safe slots, and everything else drops.** A player marks a limited number of items as
safe; those survive into their held inventory on respawn. Everything unmarked drops, armour and
weapons included — except that an item whose condition has reached 0% is destroyed rather than
dropped.

**Industry does not reach into two inventories.** Crafting while docked is a transfer followed by a
craft, which is simpler and honest about where the goods went.

### Three things those answers run into

1. **There is no mass anywhere.** `ItemDef` carries `VolumeM3` and `Inventory` carries `CapacityM3`
   (`Entities/Items.cs:28,133`) — volume, not weight. A 50 kg limit needs either a new `MassKg` on
   items, or the limit restated in m³. Worth deciding deliberately rather than adding a second
   dimension by accident: two capacity systems that disagree is a bug generator, and a hauling game
   only needs one number to be interesting.
2. **`stamina` does not exist yet.** It is one of the eight skills 101 seeds, and 101 is blocked on
   102 deciding where its XP comes from — which the design bible explicitly leaves open. So
   capacity-from-stamina is blocked behind both; a flat 50 kg is not.
3. **The death rules extend ADR-0006 rather than implement it.** That ADR settles cause-based loot
   destruction and acquisition-value insurance; safe slots, dropping on death and destruction at 0%
   condition are new rules on top. They want an ADR of their own or an amendment, not a task comment
   — ADR-0006 going quietly inert once already is why the roadmap reconciliation rule exists.

Probably wants an ADR: it changes the shape of the economy rather than an implementation detail, and
M4's premise is hauling.

## 113 — The automation run sometimes stops two tests early, and looks green doing it

**Pending. Observed twice on 14 August**, and worth fixing before it costs somebody a wrong
conclusion.

The full suite occasionally ends during the second-to-last test — `SpaceMMO.Walk.ReachesTopSpeed`
both times — with **exit code 0, zero failures, and no completion line**. Counting
`Result={Success}` alone gives a plausible-looking number (163 or 166 instead of 165) and reports
success, which is how it went unnoticed the first time: it was written off as a log-flush artifact,
and it is not.

What is known:

- **Not the test.** `SpaceMMO.Walk` run alone passed 6 of 6 with the completion line, three times.
  `ReachesTopSpeed` is 720 calls to a pure function with no world and no rendering.
- **Not reproducible on demand.** Three consecutive full runs afterwards each reported
  `Queue Empty 165 tests performed`, 0 failures.
- **Always at the tail**, one or two tests from the end, which points at shutdown rather than at
  whatever test happens to be running. Seen three times by 14 August: twice ending during
  `ReachesTopSpeed` (second to last) and once during `TurnsAboutTheNormal` (last). Both belong to
  `SpaceMMO.Walk`, which is simply what runs last alphabetically — and both pass when that category
  runs alone.
- No crash dump, no assert, no error line — the log simply stops.

**The mitigation is already in use and should stay:** assert the
`...Automation Test Queue Empty N tests performed` line and the expected N, never a bare count of
successes. A count cannot distinguish "all passed" from "stopped early having passed everything it
reached", and those are very different claims.

Worth doing eventually: run with `-stdout -FullStdOutLogOutput` and capture the exit path, or bisect
by running the suite minus the last category. Low priority while the completion-line check holds,
but it undermines every "N tests pass" claim until it is understood.

## 114 — Docking survives a restart but the ship does not

**Pending. Found by Joe, 14 August**: docked at DeepDock, closed the game, restarted. `G` correctly
said nothing was in range — and `Tab` still opened DeepDock's station overlay.

Nothing there is misreporting. `Character.DockedStationId` is persisted (`Entities/Characters.cs:86`),
so the character really is still docked; the ship is simply respawned at a default position rather
than at the station it is docked to. The record and the world disagree, and the station overlay is
only the first thing to notice.

**The sharper consequence is that presence is enforced against the record alone.**
`RefuseIfNotPresentAsync` asks `DockingService.IsDockedAtAsync` and never asks where the ship is
(`CharacterEndpoints.cs:490`). So a player who quits while docked resumes with full market and
hangar access to that station from anywhere in the system — a restart is a free trip. Harmless while
one person is playing and exactly the shape of thing that stops being harmless later.

Two ways, and it is a design choice rather than a fix:

1. **Spawn a docked character at their station.** Keeps the fiction, keeps the record true, and is
   what a player would expect — you log back in where you left off. Needs the spawn path to place a
   pawn against a station's position rather than a default one.
2. **Undock on login.** One line, loses the state, and leaves a player who quit inside a station
   floating outside it.

Worth preferring 1. It also gives 107's login screen somewhere sensible to hand off to, and it is
the only one of the two that closes the presence hole rather than papering over it.

Not caused by the HUD work; the station overlay made a long-standing seam visible.

## 96 — Author world content graphically

**Pending.** Raised 11 August; ADR-0011 makes it pressing, because a cave is a shape rather than a
point and typing a shape into JSON by hand is worse than typing a position.

**The constraint, and it is not negotiable:** `data/*.json` stays the source of truth. Content
reaches the game as `data/` → `--seed` → Postgres → C# API → HTTP → client, and the API is the
authority. It cannot read `.uasset` or `.umap`, so authoring *in* a UE level and leaving it there is
not an option — anything graphical has to write the JSON back out. Editing content in the editor and
forgetting the export would produce a world that looks right in the editor and does not exist in the
game, which is the worst failure mode available.

Three approaches, cheapest first:

1. **Capture in game.** A dev key that logs the normalised direction from the body centre at the
   player's position, ready to paste. Everything is authored as a direction — deposits, stations,
   and now caves — so this is small, needs no editor work at all, and solves placement for a single
   point immediately. It does not help with shape.
2. **An editor utility that reads and writes `data/`.** Loads `origin.json`, spawns a preview actor
   per deposit, station and cave on a body, lets them be dragged and shaped, and serialises back.
   This is the real answer to "can I adjust it graphically", and it is bounded work: UE already has
   `FJsonSerializer`, and the project already runs an editor MCP plugin, so editor tooling is not
   foreign here.
3. **Author in a level and export.** Rejected on the same grounds as above: two sources of truth,
   and the export is required regardless, so the level buys nothing.

Whatever is built, **re-seed after editing** — `dotnet run --project services/SpaceMMO.Api -- --seed`
is the only thing that applies changes under `data/`, and restarting the API does not.

## 97 — Settlements

**Pending. Decided 11 August: a settlement is a cluster of existing station kinds — no new kind.**

A town is several stations placed close together: `Housing` for the homes, `Social` for the square,
`TradingHub` or `Spaceport` where it earns its keep. That needs no enum value, no migration and no
serving change — it is entirely content, authored the way stations already are, and it gives a
settlement a footprint rather than a point for free.

What it does need is a way to say *these stations are one place*: a name a player can be told to go
to, and something that stops a cluster reading as four unrelated cubes in a field. Whether that is a
`settlement` key grouping station entries, or purely a naming convention with no schema at all, is
the next decision — and the second costs nothing, so it is worth trying first and only adding the key
when something actually needs to query by it.

The reasoning below was written when the plan was a new `StationKind`. It is kept because the second
half of it still applies with more force: whatever a settlement is made of, it will render as
identical engine cubes until somebody gives stations a look.

`StationKind` in `services/SpaceMMO.Domain/Universe/UniverseEnums.cs` already carries `TradingHub`,
`Spaceport`, `Housing`, `Social` and `Capital`, so adding a value is a one-line change plus content.
Two things found while checking that, both worth knowing before starting:

**Do not call it `Settlement`.** That word is already taken, and not loosely: `Domain/Market/
Settlement.cs`, `SettlementTests`, `SettlementIntegrationTests` and `MarketService` all use it for
the settling of trades. A `StationKind.Settlement` would collide with the market domain in every
search anyone ever runs. `Outpost`, `Colony` or `Town` are all free.

**Adding the kind is nearly free; making it look like one is not.** The client treats `kind` as an
opaque `FString` — parsed by `TryGetStringField` and, as far as the code shows, used only in a log
line at `SpaceMMOStationActor.cpp:122`. Every station is the same engine cube scaled and lifted onto
the terrain, whatever its kind. So the enum value, the JSON and the serving cost almost nothing, and
the entire visible difference between a trading hub and a town is work that does not exist yet.

Also worth deciding rather than drifting into: whether a settlement is one station with a bigger
footprint, or several stations of existing kinds placed close together — `Housing` and `Social`
already describe parts of what a town is, and a cluster might get there with no new kind at all.

---

## Done

Nothing yet under this file's numbering.
