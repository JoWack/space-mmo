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

**Pending.**

The design bible separates two skills: `gathering`, "hand-collecting surface materials", and
`mining`, "tool-gated ore extraction from deposits and asteroids". The client has
`SpaceMMOGatheringComponent` and no mining equivalent — **verified**: there is no mining component or
input verb in `client/Source`. `mining` does appear in `SkillAwards.cs`, `Items.cs` and
`Universe.cs`, so the server side may already know about it; **which parts exist is unverified.**

Start by reading the server before writing any client code, because the milestone needs the *loop*
rather than a second gathering verb, and the tool gate is what makes mining a different action.

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
