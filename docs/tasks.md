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

Milestones live in `README.md` under Roadmap. **M5 — an interface — and M7 — a world worth being in
— are the ones being worked, as of 31 August.** Tasks 91 to 95 are derived from M3's sentence rather
than recovered from any list; M3 closed and this line went on naming it as current for a fortnight,
which is the same drift the README records four times and is worth correcting here the moment it is
noticed rather than at the next reconciliation. Where a task asserts something is missing, it says
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

**The lookup this needs has a second customer, and it is not a cave.** Noted 19 August, from the
question of how a settlement gets built (see 97). A town needs level ground under it on a curved,
noisy planet, which means the height field being overridden inside an authored region — the same
sentence as a cave, with a different shape inside it. Both need the client and the dedicated server
to agree, because a player walks on both.

So the interface worth building is not `IsThereACaveHere` but something closer to **"what does the
ground do here"**: an authored region, a rule for how it modifies the height field, and one place
every consumer asks. ADR-0011's consequence that "whatever reads the cave lookup does not care how
the lookup answers" already points this way; this is the same argument for the shape of the *question*
rather than the answer.

Worth knowing before the interface is drawn rather than after. Two customers in mind is cheap;
retrofitting the second is not — and a settlement pad is a much simpler modifier than a cave, so it
is a good first implementation of a lookup that has to work for both.

**96 is built now, and it does not solve this yet.** The editor utility places deposits and stations
by dragging them, and it reads and writes `data/universe/origin.json` without disturbing a byte of
what it did not change — so the machinery for authoring graphically exists. What it cannot do is
author a cave, because a cave has no schema to write and no shape gizmo to drag. This task decides
the first, and gets the second cheaply once it has: the tool's document layer is agnostic about what
kind of thing an entry is.

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

**Done.** Layout agreed with Joe, 12 August — Option A, contextual — and every context built and
confirmed on screen since: the flight readout, the on-foot identity block, the deposit prompt above
the reticle, skills on `K`, the station overlay with its four tabs, the inventory screen on `I`, and
transient messages. The debug panel it replaced is retired.

Two things it taught that outlived it, both now in `CLAUDE.md`: a widget cannot restore its own
visibility once it has dropped it, because Slate drives `NativeTick` from `Paint` — which is why
`UpdateHudContext` lives on the controller and not in the widgets. And `HitTestInvisible` means
"self *and all children*", which is how the inventory screen came to be undraggable while looking
entirely normal.

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

**Done 19 August**, confirmed by playtest across every case: a wrong password, an unreachable
server, a successful sign-in, and a remembered session surviving a relaunch.

Credentials were read from `secrets/player-login.txt`, and `play.bat` explains why: command lines
mangle values here, and `-BackendEmail=someone@gmail.com` has arrived as `someone@gmail .com`,
producing a 401 that looks exactly like a wrong password. A fine developer affordance and not
something a player can be asked to do.

**An overlay, not a menu level.** It sits on top of whatever the world is doing and hides once there
is a session. A separate map is several times the work and is task 110's job; building one here
would be doing 110 badly in advance and unpicking it later.

**Decided: the credentials file wins over everything, including a remembered session.** It exists so
that two clients on one desktop can be two different players, and a remembered token would quietly
make both of them whoever signed in last — which is the one thing that arrangement is for. So the
order is: file or command line, then a remembered session, then the screen, then today's behaviour
if no screen is configured. That last case is what every automated run is in.

**Decided: a "remember me" tick, on by default, writing the session to `Saved/session.txt`.** A
bearer token in a plain file is a real cost, and it is why this is a choice rather than automatic:
anything that can read the file can act as the account until the token expires. The alternative was
retyping a password on every launch of a game played on one person's own machine. Worth revisiting
if there is ever a shipped build.

**Expiry is checked on restore rather than left to the first 401.** A silently restored dead token
gives a game that looks signed in and refuses everything, which is the least explicable state to be
in — and the screen exists precisely to be shown instead.

**Deferred to 110: character select.** Today's behaviour stands — `-CharacterId=` if given, otherwise
the first character on the account. A picker built here is the thing 110 would replace.

While the screen is up the mouse is released and every other HUD element is hidden: a HUD over a
world you have no character for is a set of readouts about nobody.

`SpaceMMO.HUD.LoginSaysWhichThingWentWrong` covers the wording, which is the part with decisions in
it: a 401 becomes "Wrong email or password", nothing at all becomes "Could not reach the server"
rather than a blank line, and anything else is shown as the server said it. The last matters here
specifically — an API refusing to start over an unapplied migration has already once read as "cannot
identify my character" and cost a session.

**Not covered by tests: the gate itself.** Precedence, the remembered-session round trip and the
screen coming down on success all need a world, a game instance and a filesystem, and are playtest
only. That showed: the playtest found three faults nothing else could have.

- **The screen subscribed to one delegate where the credentials path subscribes to three**, so
  signing in produced a session and then nothing — no identity, no inventory, no skills, and no error
  saying why. All three are bound once before either path chooses how the session arrives, which is
  the only arrangement where they cannot drift apart again.
- **The ship flew while somebody typed their email.** `FInputModeGameAndUI` still delivers to the
  pawn. The login screen is `UIOnly` — the only screen here that should be modal, because you can
  reasonably fly with an inventory open and cannot reasonably fly while typing a password.
- **The HUD was created after identity resolved**, so `BeginIdentifying` could not ask for a sign-in
  on a screen that did not exist yet. It logged "no login screen configured" and the screen was
  created 128 ms later.

And one that was not a playtest fault but wasted the same evening: the two text boxes were a bare
`UPROPERTY`, which is invisible to a Blueprint graph. `BindWidget` makes the pointer arrive and does
nothing about visibility, and neither produces an error — so it looks exactly like a name that does
not match.

## 108 — Inventory and transfer screen

**Done.** Blueprints wired and confirmed by playtest: reading holdings, dragging between containers,
and a quantity prompt on the drop. Distant hangars are listed and dimmed.

**Decided 13 August: its own overlay, opened with `I`** (verified free against
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

**Written 14 August.** `USpaceMMOInventoryScreen` +
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

**Containers exist, 15 August.** `InventoryService.GetOrCreateCarriedAsync` mirrors the hangar
factory — keyed on the character alone, since there is exactly one and it travels with them. Created
at character creation, and `--seed` backfills anyone older, the same idempotent shape as the starting
stake. Twelve characters were given pockets on the first run.

The inventory response now carries **`inventoryId` on every stack and instance**, plus a
**`containers` list of every container owned whether or not anything is in it**. Both were required
before drag-to-transfer could exist at all: transfer is addressed by inventory id, contents cannot
describe an empty container, and the first haul anybody makes goes into a hold that is empty by
definition.

**Ship holds are not part of this** and cannot be until
[ADR-0012](adr/0012-a-ship-is-earned-and-carries-its-own-hold.md) lands — no character owns a ship
instance, so a hold has nothing to hang from. **Transfer is therefore carried ↔ hangar only**, which
is enough to build and exercise the drag interaction against.

**Docking creates the hangar, 16 August.** Found by Joe: docked at DeepDock, the inventory showed no
DeepDock entry, so there was nowhere to drop goods. Nothing was misreporting — a hangar was only ever
created when goods *arrived* (gathering, industry, a market fill), so a station nobody had traded at
had no container to list. Invisible while everything teleported to station 1 (111), and immediately
visible once a player could move goods by hand.

`DockAsync` now ensures one. Docking is the moment a character gains access to a station's storage,
and the factory's own comment says auto-creation exists so renting storage is not a step between a
player and trading.

**Read-only first, decided 13 August.** Moving goods needs a selection model and a quantity
affordance and roughly doubles the work; seeing what you own and where is a real screen on its own,
and it is the half that is missing today.

**Transfer is drag between groups** — Joe's preference, 13 August — and is written as of 15 August,
awaiting its Blueprint half.

**Every line carries its container, headings included.** That is what makes a whole group a drop
target without a widget per group to drop onto, and it fell out of building rather than out of the
sketch. Grouping is keyed on inventory id rather than kind and station, because two hangars at one
station would otherwise merge and leave a drop with nowhere unambiguous to land.

`CanDrop` is pure and static so all four rules are testable without a mouse: cargo must be
draggable, the destination reachable, and — the one easy to get wrong — a drop back into the
container the goods are already in is refused. That would send the server a move from a place to
itself and redraw identically, which reads as the drop being lost.

**The quantity prompt belongs to the Blueprint.** C++ fires `OnQuantityRequested(Name, Max)` and
waits for `ConfirmQuantity` or `CancelQuantity`; how you ask for a number is a look rather than a
rule. Always asked for a stack rather than behind a modifier key, because a modifier is invisible
until somebody tells you about it — and the prompt arrives pre-filled, so moving everything is still
one keystroke. Instances skip it entirely and move whole.

**Opening `I` releases the mouse**, since dragging needs a cursor and the game holds it. Closing
gives it back only if opening was what took it: somebody who pressed `M` for their own reasons
should not have it snatched back.

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

**Done 16 August.** Supersedes the workaround half of 105, which is now closed.

The book could only be seen for an item the player already held, because the fetch was keyed to their
selected holding. `GET /market/listings?stationId=&search=` now answers with the **whole tradeable
catalogue** — `Raw`, `Refined`, `Component`, `Consumable` — not just items somebody happens to be
trading, and the screen lists all of it with best ask, best bid, quantity for sale and the faction
guaranteed price. Selecting a row fetches its book into the existing panel.

**The catalogue is the whole catalogue on purpose.** Listing only what has orders is the smaller
query and was the obvious build, but it makes the market unusable in exactly the case a new economy
is always in: nothing is for sale, so nothing is listed, so nobody can place the **buy order** that
would tell a producer the demand exists. An empty market that cannot be seeded is a dead one. Ruled
out 16 August.

Two suggestions are offered when placing an order, and both are suggestions rather than defaults —
the price field is always typeable:

- **match market** — the current best ask or bid, present only when somebody is trading it
- **guaranteed** — the faction price, present only for goods a faction stands behind, and only when
  selling, because standing orders buy and never sell

An item with neither is priced by hand, which is the normal case for anything new.

**Ruled out: `Min`/`Max` over `Price.MinorUnits` in the EF query.** It compiles — the property is
behind a value converter — and then 500s at runtime. Aggregation is done in memory over the
station's open orders instead. Anything similar over a converted property will fail the same way.

**Ruled out: showing a missing price as `0.00 cr`.** Free and zero are different claims, and a market
that says an unpriced good is free is worse than one that says nothing. Missing renders `—`, and
quantity renders blank rather than `0`. `SpaceMMO.HUD.MarketNoPriceIsNotAZeroPrice` fails if the dash
ever collapses back into a formatted zero — verified by collapsing it, 16 August.

`economy-design.md` §7 already names price history as the source for this; not used yet.

**`H`, `N` and `B` are retired**, with their handlers, their bindings and their `DefaultInput.ini`
mappings. They cycled a holding, listed ten of it at a guessed price and bought the best ask — the
only way to reach a market before there was a screen. The catalogue replaced all three, and
`SellableHoldings`, `TryGetSelectedHolding` and `ListingPriceFor` went with them. Those keys are free.

**The book panel follows the catalogue selection, and nothing else.** It was fed by both that and the
player's selected holding, and the holding won on every two-second poll — so clicking a row fetched
the right book and had its heading rewritten with the old item's name a frame later. On screen that
read as the prices flashing and reverting. It also now distinguishes *loading* from *nobody is
trading this*, because rendering the previous item's orders under a new name is a price for the
wrong thing.

**Prices are typed as credits.** `ConfirmOrderInCredits` parses "20.00" exactly, digit by digit
rather than through a float. The conversion was the Widget Blueprint's job for exactly one playtest,
and the first order placed through it went in at a hundredth of the intended price — no error, a
plausible figure, real money. That order is what produced 119.

**The book is rows, not text.** Quantity and price have their own columns, and each resting order
carries a button labelled with what the *player* would be doing — `Buy 4` under SELLING, `Sell 100`
under BUYING. Taking one places the opposite side at that order's price for its remaining quantity:
it buys the **price**, not the order, because matching is price-time priority and somebody resting at
the same price who got there first fills instead. Same goods, same price.

**Your own rows show but cannot be taken.** The book carries an `IsYours` bool — a flag rather than a
name, decided 17 August: who placed an order is nobody's business, and the only thing the client
needs is whether taking it would be a self-trade. It is ownership-checked, so it cannot be used to
work out who owns what one order at a time. Without it, clicking Buy on your own ask places a buy
that cannot cross and simply rests — which is how an ask at 0.01 cr and a bid at 20.00 cr came to sit
on one book looking like a broken market.

`BuildMarketPanel` and the old `MarketRows` text panel are gone with it. The `bLoaded` distinction it
carried moved into `BuildBookRows` rather than being deleted: an empty book and an unanswered one
look identical and mean opposites, and two frames of "no market exists" for an actively traded item
is a conclusion a player would act on.

Follow-on: 116 (drag a stack onto the market to sell it) is still open.

## 110 — Menus

**Pending.**

No main menu, no character select, no settings. A character is chosen today with `-CharacterId=10`
on a command line, or by taking the first one on the account.

## 105 — You can only see the book for something you already own

**Closed 16 August by 109.** Decided 13 August: a station's market screen shows everything for sale
there, and has a search. Both halves, not one or the other — browsing answers "what is there?",
search answers "who has the thing I came for?", and a market with only one of them fails whichever
question the player actually arrived with.

That rules out the third option below (listing only what the station has asks for) as the whole
answer. It did not survive as the browsing view either: 109 lists the whole tradeable catalogue,
because a station with no orders yet would otherwise show an empty screen nobody could seed a buy
order from.

Found by playtest, 12 August.

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

# Found while building, and not yet in a milestone

Everything below turned up while something else was being built, or was split out of a task that
had grown two halves. They are filed here rather than under M6 — which is where they had drifted,
combat tasks and gathering bugs and test tooling under one heading — because a milestone is a claim
about what the game will be able to do, and none of these are that yet.

Task 142 belongs to **M1**, where EconSim was built. Several belong to **M7 — a world worth being in**, added to the roadmap on 18 August: 121, 122, 124,
125, 126, 129, 130, 131, 132, 139, and the existing 89, 96 and 97. Tasks 133 to 138 belong to **M5 — an
interface**, which was widened on 29 August to name perspective and controls: `design-bible.md` §8
describes them and no milestone had ever hosted it. They are left in place here rather than moved, because a task's
number is how it is referred to months later and shuffling blocks around a file this size is how
content gets lost.

## 111 — Gathering and industry ignore where you are

**Done 16 August**, confirmed by playtest. Found by Joe, 14 August, refining at DeepDock and watching
the output appear at the capital.

`ASpaceMMOPlayerController::StationId` is a hardcoded `1` — its own comment says "Not yet chosen per
player" — and it is sent as the storage station for gathering *and* as the station for `StartJob`.
`IndustryService` then uses that one station for both halves: it consumes inputs from
`GetOrCreateStationHangarAsync(characterId, stationId)` and deposits outputs back into it. So a job
started anywhere in the system is really run at station 1, which is self-consistent and therefore
invisible until somebody docks elsewhere and looks.

**Done, 16 August**, once transfer existed to make it bearable. The recorded fix — "send
`DockedStationId()`" — turned out to be right for only half of it.

**Crafting** is a station service, so it uses where the player is docked and refuses locally with
"Dock at a station to start a job" when they are not.

**Gathering** happens at a rock, on foot, where `DockedStationId()` is always 0. There is no station
to choose, which is precisely why the client invented a fixed one — so gathered material now goes
into the character's own hands, which is ADR-0012's fifth point. The station left the whole gather
path: the request record, the service method, the client call and the JSON body. A parameter that is
ignored is a lie the next reader has to unpick.

**Crafting also draws on what the player is carrying**, decided 16 August. Choosing a recipe while
docked moves whatever is missing from their pockets into that hangar and then crafts, in one
transaction. That keeps the rule Joe settled in 112 — transfer, then craft — while removing the
chore. Only while docked at that station: without the check, starting a job somewhere a character is
nowhere near would haul goods out of their hands and across the system, which is 114's hole and
worse, because it would move things.

Two things worth keeping from building it:

- **131 data tests passed after the destination of every gathered item changed.** Nothing asserted
  where ore lands. `GatheredMaterial_GoesIntoTheCharactersHands` now pins the container's kind, its
  owner, and that it has no station.
- **`AddAsync` and `RemoveAsync` leave saving to the caller**, and the consumption below finds its
  stack with a database query. Topping up a hangar that already held some of an input works by
  accident — the query returns the very entity just changed — but topping up one that held none
  leaves the new row in the change tracker where the query cannot see it, and the job fails for want
  of material sitting in the same transaction. The top-up flushes before the consume reads.

## 112 — Goods are held, not teleported

**Done 31 August**, by [ADR-0014](adr/0014-capacity-is-volume-and-it-binds-everywhere.md). Belongs
to **M4 — goods that move and gear that matters**. Recorded as Joe's direction on 14 August, before
anything was built.

**Most of it turned out to be built already**, and the task had gone stale saying otherwise — which
is worth recording as much as the work is. Measured rather than assumed on 31 August: gathering was
already depositing into carried inventory, crafting already drew on carried stock while docked, and
transfers were already explicit and carrying their cost basis. Goods had stopped teleporting some
time ago and nobody had come back to say so.

What was left was the half that makes any of it matter, and the code said so itself in
`InventoryService.TransferAsync`: *"Volume is not checked. CapacityM3 exists on the row and hangars
are created at zero, and nothing anywhere enforces it yet."* Every inventory in the game was
infinite, so a ship's hold was decoration and nothing distinguished a shuttle from a freighter.

Today everything gathered or crafted appears in a station hangar wherever the player is, which is
what 111 is about at the mechanical level — but the deeper point is that **goods never travel**. What
Joe wants instead:

- Gathering and crafting deposit into **carried** inventory (or a ship's hold).
- Moving goods to a station is a deliberate transfer, not automatic.
- Crafting and refining, while docked, may draw on **both** carried and station inventory.

This is the direction 99 already pointed at without spelling out: `CharacterCarried` and `ShipHold`
both exist, are both documented in the enum, and nothing routes anything into either.

**It needed `CapacityM3` to start meaning something**, which is what ADR-0014 settles: capacity is
volume, no mass is added, and the rule binds in `AddAsync` because every route into a container goes
through it — gathering, crafting output, a market purchase, a quest reward, a transfer. On transfers
alone it would have given a hold you cannot fill by dragging and can fill by mining into, which looks
like a rule and is not one.

### Settled by Joe, 14 August

**A character on foot carries goods, limited by weight.** Default 50 kg, raised by the `stamina`
skill, and further by a backpack — an equippable, so M4's "equippable tools, weapons and armour".

**Death: a few safe slots, and everything else drops.** A player marks a limited number of items as
safe; those survive into their held inventory on respawn. Everything unmarked drops, armour and
weapons included — except that an item whose condition has reached 0% is destroyed rather than
dropped.

**Industry does not reach into two inventories.** Crafting while docked is a transfer followed by a
craft, which is simpler and honest about where the goods went.

### Three things those answers ran into

1. **There is no mass anywhere**, and ADR-0014 settled it as volume. The reading that decided it was
   of the authored pack rather than of the schema: a unit of ferrite ore is **0.4 m³** and a resource
   node holds two hundred of them. These are ship-scale numbers, so 50 kg was never going to survive
   contact with them whichever dimension won. Carried is **6 m³** — fifteen ore, chosen by Joe on 31
   August — and hulls carry an authored `HoldCapacityM3`: shuttle 80, freighter 360.
2. **`stamina` does not exist yet.** It is one of the eight skills 101 seeds, and 101 is blocked on
   102 deciding where its XP comes from — which the design bible explicitly leaves open. So
   capacity-from-stamina is blocked behind both; a flat 50 kg is not.
3. **The death rules extend ADR-0006 rather than implement it.** That ADR settles cause-based loot
   destruction and acquisition-value insurance; safe slots, dropping on death and destruction at 0%
   condition are new rules on top. They want an ADR of their own or an amendment, not a task comment
   — ADR-0006 going quietly inert once already is why the roadmap reconciliation rule exists.

### The case that was only visible from inside the work

ADR-0014 said a full container refuses the whole delivery: half a delivery arriving and the rest
evaporating is a silent loss of a player's property, and the same rule on a market purchase is a
silent partial refund.

**Then mining stopped working entirely.** A swing yields twenty ore, which is 8 m³ against a pack of
six, so the rule as written refused every swing — including the first one, into an empty pack. The
ADR was amended the same day it was accepted.

The distinction it had missed is whether refusing destroys anything. A purchase or a transfer moves
goods that already exist, and refusing part of one destroys the rest. **Ore that will not fit is
still in the ground.** So gathering takes what fits and leaves the remainder in the node — and the
node is drawn down by what was taken rather than by what was swung for, which is the assertion that
would catch a full pack quietly deleting a node's contents.

Two other tests had to be widened rather than corrected: the gathering and industry suites are about
yield and about what crafting reaches for, and a six cubic metre pack made capacity the constraint in
all of them. Failures there would have read as gathering bugs. They ask for room deliberately now,
and say why.

### A zero with three reasons

Found in the playtest after the rule started binding: mining stopped and the message read **"Nothing
yet - give it a moment"**, so the answer looked like patience when it was in the player's own
pockets. The only way to discover otherwise was to move something to a hangar and try again.

`FormatGatherMessage` already carried the comment *"Nothing yielded, and the reason matters. Too soon
is worth waiting out; spent is not"* — and capacity added a third reason it had never anticipated.
`GatherResult` says which now, and the full pack is checked **before** the deposit: being told a
deposit is worked out while the answer is in your own pockets sends somebody walking to another rock
to meet the same wall.

**A full pack behind a running cooldown still reads as "give it a moment"**, because at that instant
waiting genuinely is the next thing to do. The test says so rather than leaving it to be rediscovered
— it resets the clock before asking, or it would pass for the wrong reason.

### Not in this, deliberately

- **Capacity from `stamina`, and backpacks.** Both were in the 14 August direction. `stamina` does
  not exist — it is one of the eight skills task 101 seeds, and 101 is blocked on 102. A flat figure
  needs neither, and both are later changes to one number rather than to the rule around it.
- **The death rules.** Safe slots, dropping on death, and destruction at 0% condition extend ADR-0006
  rather than implement it and belong to **M6**. Folding them into a document about capacity is how
  ADR-0006 came to be quietly inert the first time.

## 113 — The automation run sometimes stops two tests early, and looks green doing it

**Understood and fixed, 18 August.** Observed twice on 14 August; the cause was in the engine and
took reading rather than reproducing, which is just as well because it never reproduced on demand.

**It was the flag that ends the run.** `-testexit` is a log watcher — `FOutputDeviceTestExit`,
`LaunchEngineLoop.cpp:397` — that substring-matches every line from any thread and then calls
`RequestExit(Force=true)` on the next engine tick. A forced exit means no clean shutdown and no
guaranteed log flush, so losing the last test results and the completion line is not an anomaly, it
is what that code does. The "log-flush artefact" it was written off as the first time was exactly
right about the mechanism and wrong to dismiss it.

**The worse half nobody had noticed: the exit code never meant anything.** Nothing on that path sets
`GIsCriticalError`, so the editor exits 0 with failing tests (`AutomationCommandline.cpp:491-504`).
Every "exit code 0" in this repository's history carried no information; the failure count came from
grepping the log by hand, which is the only reason failures were ever caught.

**Ruled out: `Automation SoftQuit` on its own.** It is the right command — the engine re-queues it
behind a pending run, sets `GIsCriticalError` from the reports, and shuts down gracefully, so the log
is complete and the completion line is written. It then hangs partway through editor shutdown and
never exits. Measured 18 August: 29 minutes on a two-test run, killed by hand.

**Ruled out: plain `Quit` in `-ExecCmds`**, which is what `setup.md` used to warn about. It is the
*engine's* quit console command rather than the automation one, which is why it exits before any
test runs — a naming collision, not a flaw in the approach, and the reason `-testexit` was reached
for in the first place.

So `scripts/tests.ps1` runs with `SoftQuit`, waits for the completion line, and supplies the
terminator itself. The log is the source of truth and the script's exit code is the contract; the
editor's is untrustworthy under either flag.

It also asserts things that have each cost time here:

- **The completion line**, never a count of `Result={Success}`. A count cannot tell "all passed"
  from "stopped early having passed everything it reached", and it over-counts anyway — 166 for a
  165-test suite, because cluster lines match too.
- **`LogInit: Command Line:` contains `RunTests`.** `Start-Process -ArgumentList` drops the quotes
  around `-ExecCmds`, the value arrives split on spaces, and the editor starts, reports "Ready to
  start automation", queues nothing and idles indefinitely. That reads as a slow suite; it cost
  twenty minutes before the check existed. This is the machine's argument-mangling hazard again.
- **The expected count**, when given.

The log is deleted before each run, so a run that dies early cannot be read as the previous one's
result, and a truncated run is copied aside with its last twelve lines printed — a forced exit
leaves no error to read, so that is the only evidence there would be.

**Verified by planting a failing test**: the runner reports `FAIL`, names it, and exits 1. The full
suite reports `PASS: 176 tests, 0 failures` and exits 0.

## 114 — Docking survives a restart but the ship does not

**Built 18 August, option 1.** Found by Joe, 14 August: docked at DeepDock, closed the game,
restarted. `G` correctly said nothing was in range — and `Tab` still opened DeepDock's overlay.

`Character.DockedStationId` is persisted, so the character really was still docked; the ship was
respawned at a default position. The record and the world disagreed, and because presence is
enforced against the record alone (`RefuseIfNotPresentAsync` asks `IsDockedAtAsync` and never asks
where the ship is), a restart was a free trip to that station's market and hangar.

**Chosen: spawn a docked character at their station.** It keeps the fiction, keeps the record true,
and is the only one of the two options that closes the presence hole rather than papering over it.
It also gives 107's login screen somewhere sensible to hand off to.

**Ruled out: undocking on login.** One line, and it discards state the player had — leaving somebody
who quit inside a station floating outside it.

The docked station rides along with **identity resolution** rather than being fetched separately:
`/accounts/resolve-character` is the one call the game server already makes at the moment a
connection becomes a character, and that is exactly when the answer is needed. `ResolvedCharacter`
gained `DockedStationId`, null for anyone in flight — null rather than a sentinel, because a zero
that meant a station would land every new character at whichever station held that id.

### What the implementation ran into

**The station may not exist in the world yet.** Station actors are spawned from backend data and
identity resolves on its own schedule, so `ResumeDockedAt` records the intent and the component's
tick carries it out once the actor appears. It logs when it does — moving somebody's ship without
being asked is not a thing to do silently — and gives up loudly after 30 seconds, because a resume
that waits forever leaves the player exactly where this task found them with nothing to tell that
from the original bug.

**The resume has to run above the existing range check**, which returns early while
`DockedStationId` is zero. That is precisely a freshly spawned ship's state, so a resume waiting on
it would have waited forever.

**Placed at the station's own position, not offset beside it.** Docked means at the station, and it
is the only placement guaranteed to sit inside that station's own docking range — a guessed offset
would fall outside the range of any station whose range is smaller than the guess, and the range
check would undock them again on the next pass.

### What is verified, and what is not

The API half has tests: `It_says_where_the_character_is_docked` and
`A_character_in_flight_is_docked_nowhere`.

**The resume itself has none.** It needs a world with station actors, a possessed ship and a
resolved identity, which the headless suite has none of. It is covered by playtest only, and the log
lines exist so that a playtest can tell "put back" from "gave up" from "never ran".

**The presence hole is closed only while the resume succeeds.** If it times out, the record is still
true and the ship is still elsewhere — the same hole, now loud instead of silent. Closing it
properly would mean the API knowing where the ship is, which it cannot; that is a bigger seam and
belongs with 115.

## 115 — A ship is a thing you earn, and its hold belongs to it

**In progress.** Decided 15 August:
[ADR-0012](adr/0012-a-ship-is-earned-and-carries-its-own-hold.md). The ADR is the decision; what
follows is the shape of the work and what it runs into.

### What was already true, and the ADR does not know it

Measured on 31 August before starting, and it makes this task much smaller than it reads:

- **The opening already changed.** ADR-0012 calls it "the largest cost" that "every connection
  currently spawns flying" — `SpaceMMOGameMode` sets `DefaultPawnClass = ASpaceMMOCharacterPawn`
  now. Task 120 did it a fortnight after the ADR was written, and nobody came back to strike the
  cost out.
- **Crafted hulls are already owned instances.** `IndustryService` creates an `ItemInstance` for any
  non-stackable output, so `hull_shuttle` off the questline is already a thing somebody owns.
- **What stands in for a ship is a prop.** The game mode spawns an unowned `ASpaceMMOShipPawn` 30 m
  from the player so boarding could be tested. It belongs to nobody and carries nothing.

### Done: a hull has a hold

`GetOrCreateShipHoldAsync` — keyed on the hull instance exactly as `Inventory.ShipItemInstanceId`
has described since the first migration, with the capacity read from the hull's own
`HoldCapacityM3` (ADR-0014). Before this, **`ShipHold` appeared exactly once in the whole service
layer, in a comment**: the schema said the game worked one way and the game worked another.

It is the first thing in this game that has ever told a shuttle from a freighter — 80 m³ against 360,
which is one resource node against four and a half.

Four rules the tests hold down, each of which would be silently wrong otherwise:

- **Keyed on the hull, not the owner.** Hanging it off the character is one line shorter and works
  until somebody owns a second ship, at which point a fleet shares one boot.
- **Re-rating a hull in content reaches the holds that already exist.** Assigning capacity only on
  creation is the mistake the carried inventories made, and it applied the rule to nobody who
  already had pockets.
- **A hull nobody owns gets no hold.** `ItemInstance.InventoryId` is null once destroyed, and a
  container addressed to nobody is the state ADR-0006 calls being inside the explosion.
- **An unrated hull carries nothing rather than everything.** Zero means unlimited on that column,
  which is right for a station hangar and wrong for a ship: an unlimited hold is a bank account you
  can fly, and ADR-0008's planet-locked materials become a shopping list rather than a journey.

Verified by making every hold unrated and watching four of the eight go red.

### Done: summoning, and an active ship

Settled by Joe on 31 August, against the four questions ADR-0012 left open: the opening stays as it
is, summoning is free and instant, a ship stays where it was left and summoning elsewhere moves it,
and no repair or fuel gate — **and summoning happens only at stations that handle ships.**

`StationKindExtensions.AllowsShipSummoning` reads that off what the kinds already document rather
than inventing it: a `Spaceport` is "ship docking, refitting, and industry facilities" and the
`Capital` is "everything". A rule about kinds rather than a flag per station, because a boolean on
the row lets two spaceports disagree about whether they are spaceports, and the first one authored
without it is a player standing at a shipyard that will not give them their ship.

**Where a ship is needs no column, which is the part worth keeping.** An owned hull is an
`ItemInstance` sitting in an inventory, and for a parked ship that inventory is the station hangar it
was left in. Summoning moves the instance into the hangar of the station the player is standing in —
so "summoning elsewhere moves it" is one assignment, and a ship is always somewhere by construction
rather than by a coordinate somebody has to maintain. `Character.ActiveShipItemInstanceId` is the
only new state, and it is the one the ADR named as a cost.

Reachability is the same shape of rule as station stock: the hold opens when the active hull is
parked where the player is docked, and nowhere else. Somebody who flew home and left their freighter
at the capital has an active ship they are nowhere near, and its hold is exactly as out of reach as
the hangar beside it — which is what keeps hauling a journey rather than a bank transfer.

Verified by mutation, one rule at a time: never refusing somebody else's hull, and making a hold
reachable wherever you stand, each turn exactly one test red.

### Done: the server offers it

`POST /ships/summon` and `GET /ships/{id}/hold`, registered beside docking.

**A player's own token, unlike docking, and the difference is the point.** Docking records where a
ship *is*, which only the simulation knows, so it takes the service credential. Every fact summoning
depends on — being docked, at what kind of station, owning the hull — is a row the server checks for
itself, so a client that lies gets a refusal rather than a ship. The service credential is still
accepted, because the dedicated server will eventually offer summoning as a station action.

**A refusal is a 409 rather than a 400**: nothing about the request is malformed, and every one of
them is a fact that could be different in a minute — walk to a spaceport, dock, craft a hull. The
messages are written to be shown to a player as they stand.

**"You have no ship here" is a 200 with a null**, not a 404. The client asks that question every time
an inventory screen opens and having no ship to hand is an ordinary state; a missing-resource error
would have callers treating it as a fault.

### Still open

- **"Sitting in that ship" is not checked**, and it is half of ADR-0012 point 4. Nothing on the
  server knows whether a character is aboard, and being undocked cannot stand in for it — somebody
  walking around a planet is undocked too, and that would open the hold from a rock. It wants the
  server told when somebody boards, which is a change to the pawn rather than to the service.
### Done: the Ships tab's wording, as a pure function

`BuildShipRows` and `BuildShipsFooter` on the station overlay, following the panel-builder pattern:
the wording is a pure function of what the server said, testable without a widget, a world or a
backend.

Settled by Joe on 31 August: the tab lives on the station overlay rather than behind a key, because
summoning is a thing you do standing still at a station and it belongs where the rest of a station's
business is.

**Listed by category, never by key.** The inventory wire now carries `ItemCategory`, because
`hull_shuttle` and `shuttle_hull_section` are one prefix match away from listing a component as a
ship and both are already shipped. The client enum mirrors the server's numbering and is marked
append-only: the values cross as integers, so one inserted in the middle would silently reclassify
everything after it — a Hull becoming a Weapon reads as a ship you cannot summon and a gun you
cannot fire, with nothing in the payload looking wrong.

**Hulls elsewhere are still listed**, with where they are, because knowing you own a freighter at the
capital is the point of a fleet list. **And a refusal is a reason rather than a greyed button**:
"Summon" disabled with nothing beside it tells somebody they are wrong without saying about what, and
the two reasons are not equivalent — "Not a shipyard" sends a player walking and "Already here" means
there is nothing to do.

The empty tab says `No ships yet. Craft a hull to fly one.`, because owning none is the ordinary
state for most of the opening and is the state ADR-0012 deliberately creates.

Verified by mutation: filtering by name instead of category turns one test red.

### Done: the client can summon

`USpaceMMOBackendClient::SummonShip`, a `USpaceMMOShipRow` widget carrying the button, the tab on
the station overlay bound to **5**, and `ActiveShipItemInstanceId` travelling with the character so a
row can mark which ship is being flown — on the character rather than an endpoint of its own,
because every screen that cares already has the character and a second round trip to decorate a list
would be one request to answer one boolean.

**The fleet is built from what the character already owns**, not from a request of its own: the
inventory is fetched whenever anything moves, and every field a row needs is in that answer.

**`StationHandlesShips` is a copy of a server rule and knowingly so.** The station kind reaches the
client as a string, so this is the client's opinion; the server refuses regardless. The failure that
matters is asymmetric, which is why both allowed names are asserted rather than one: offering a
button that comes back refused costs a sentence, while greying one the server would have honoured
strands somebody at a shipyard with a ship they cannot call.

**The row checks `bCanSummon` before firing, and that is not redundant with the server.** The server's
refusal is the one that decides; this one stops a button already displaying "Not a shipyard" from
sending a request whose only possible answer is the sentence already on screen.

### Still open

- **No ship in the world.** Summoning records which hull is yours and gives it a hold; nothing spawns
  a pawn for it. The game mode still puts an unowned prop ship thirty metres from the player, so the
  Ships tab is testable — press Summon, watch the row become "Already here" — and the ship being
  flown is still the prop.
- **A Widget Blueprint for the tab and its rows.** `ShipRows`, `ShipsFooterText` and `ShipRowClass`
  are optional bindings, so the overlay works without them and the tab is simply empty until they
  exist. `USpaceMMOShipRow` wants a Widget Blueprint parented to it with `NameText`, `WhereText`,
  `ConditionText`, `RefusalText` and a button bound to `Summon`. The game mode still spawns an unowned prop ship thirty metres from the player so that
  boarding has something to board, and summoning a hull does not put a pawn anywhere — it records
  which hull is yours and gives it a hold.

  Those two are one piece of work and they want doing together: retiring the prop before a summoned
  ship can appear leaves a game with no ship at all, which is correct by ADR-0012 and unplayable
  until the questline is finished. Both need the interface question answered first — where
  summoning lives, and what a player with no ship sees.

- **Nobody starts with a ship.** A player crafts a hull and **summons** it — at a docking station or
  ship hangar — through the main questline.
- **A hold belongs to a ship**, not to a character. The schema already says so:
  `Inventory.ShipItemInstanceId` points at an `ItemInstance`, and shipcrafting already produces
  hulls as instances.
- **A hold is reachable only when the player is with it**: docked at a station with their active
  ship, or sitting in that ship with the inventory open.

### What this changes that is not obvious

**The game currently starts you in a ship.** `SpaceMMOGameMode` sets
`DefaultPawnClass = ASpaceMMOShipPawn`, so every connection spawns flying. Under this design a new
player starts on foot with no ship at all, and the whole opening — where you appear, what the
questline asks first, what the flight tutorial is — follows from that. This is the largest
consequence and none of it is written down anywhere yet.

**The spawn half of that is now task 120**, split out on 18 August because it depends on nothing else
here and the terrain work depends on it: iterating on how the ground looks costs a 32 km descent
while every session starts in orbit. What stays in 115 is summoning, active-ship state, and the
questline that hands over the first hull.

**"Active ship" is new state.** A character may own several hulls; exactly one is the one they are
flying or would summon. Nothing models that today, and the hold's accessibility rule depends on it.

**Summoning is a new verb**, and it needs a place: which station kinds can do it, whether it costs
anything, and what happens to a summoned ship that is left somewhere.

### What it blocks and is blocked by

Blocks the ship-hold half of the container work (108/112): a hold cannot be created while no
character owns a ship instance, so **transfer is limited to carried ↔ hangar until this lands**.

Related to ADR-0006, which already assumes a ship's hold is what is inside the explosion when a ship
is destroyed — that rule only becomes real once holds belong to ships.

**Wants an ADR.** It settles how players get their first ship, which is an onboarding decision as
much as an inventory one, and the roadmap-reconciliation rule exists because exactly this kind of
decision went unwritten before.

## 116 — Drag goods onto the market to sell them

**Done 18 August.** Planned 15 August while building 108's transfer; the plumbing was deliberately
left general for it, and none of it needed changing: the drag operation carries a whole inventory line
rather than an id, and the pairing that makes the gesture possible — market left, inventory right —
was already there.

Dropping a stack anywhere on the market tab selects that item in the catalogue and opens the sell
prompt with the dragged quantity filled in.

**Decided: the whole tab is the target, not the catalogue list.** A big target is easier to hit and
nothing else on that tab could mean anything by a drop. Drops on Industry and Quests are ignored
rather than swallowed — a stack disappearing into a tab nobody was looking at is worse than nothing
happening.

**Decided: the drop reuses the by-hand path rather than getting its own.** It selects the item and
opens the same prompt the Sell button opens. A second listing route would be a second place for the
price to be wrong, and the price is the part a drop cannot express: it says *what* and *how many*,
never *at what*.

**Decided: the prompt opens on the dragged stack, capped at everything that could back the order.**
Dragging 120 out of a hangar while carrying 40 more offers 120 and allows raising it — the drag named
a stack, not a limit. `OnOrderRequested` gained a `DefaultQuantity` pin for this.

**Ruled out: dragging from the book to buy.** Flagged as the more dangerous gesture when this was
planned, and 119 gave every book row its own `Buy 4` button — so the gesture would add a way to spend
credits by accident and no capability at all.

**Every refusal says why.** A silent refusal is indistinguishable from a drop that missed the panel,
and each of these is something the player did on purpose: a container heading, an instance (a hull or
a laser — one object with its own condition, where the book moves quantities), goods at another
station, an empty stack, or something nobody trades here. `RefuseSellDrop` is pure and returns the
reason rather than a bool; removing the reachability check fails its test.

## 117 — A key bound to a dead input component says nothing at all

**Done, 16 August.** Found by Joe: docked at DeepDock, flew to the capital and docked, undocked,
flew back — and `G` did nothing, with no message of any kind.

`USpaceMMODockingComponent` bound the key once in `BeginPlay` and guarded with a bool. **Possession
builds a new `InputComponent`**, so boarding a ship, leaving it and boarding again left the binding
attached to a dead one, and the bool stopped it ever rebinding. Nothing ran, which is why nothing was
said — every branch of `ServerDock` reports something, including "Nothing in docking range".

It survives the first two docks because the ship a player spawns in is possessed at spawn. It is the
disembark-and-reboard cycle that swaps the component.

**The gathering component had already learned half of this.** Its comment reads "Missing this was why
the key did nothing at all" — it hooks `ReceiveRestartedDelegate`, but still guarded with a bool, so
it would have broken the same way on a re-possessed pawn. Both now record *which* input component
they are bound to and rebind when it is replaced. A flag cannot tell "already bound" from "bound to
something that is gone".

## 118 — Mining needs the tool on you, not merely owned

**Done, 16 August.** Found by Joe: mining worked with the laser sitting in a hangar.

`GatheringService.GuardToolAsync` asked only that the character own an undestroyed instance with
condition above zero, anywhere. Task 94 recorded exactly why it could not be tightened at the time:
nothing routed anything into a carried inventory and there was no way to move a laser onto a
character, so requiring it carried would have made mining impossible rather than stricter.

Both halves of that stopped being true this week — characters have pockets (108) and goods can be
dragged into them. The check now requires the tool in the character's carried inventory.

A laser already sitting in a hangar must be dragged into CARRIED before it can be used, which is a
real behaviour change rather than a silent fix. Equipment slots will narrow this further when they
exist; carried is the honest middle step.

## 119 — An order you placed cannot be found or withdrawn

**Done 17 August**, Blueprints wired and confirmed by playtest. Found by Joe the same day, holding an
order he could not get rid of.

Nothing lists a character's own orders, and nothing in the client calls cancel — so once an order is
placed it rests until it fills or expires, whatever it says. Joe placed a sell order at 0.01 cr
through the pre-credits-parsing prompt and had no way to withdraw it.

`MarketService.CancelOrderAsync` exists and works, and `POST /market/orders/cancel` is wired to it.
The missing pieces are: an endpoint answering "what have I got resting?", a client fetch, somewhere
to show them, and a cancel action.

**Not merely a convenience.** Sell orders reserve goods and buy orders lock credits, so an order
nobody can withdraw is inventory and money permanently out of a player's reach. Every wrong price is
permanent until expiry.

**Decided: every station's orders, with the far ones marked rather than hidden.** Orders are placed at
a place (ADR-0008) and the server already lets any owner cancel from anywhere — it checks ownership,
not docking. Scoping the list to where the player is standing would hide precisely the order they
opened it to find.

**Decided: cancel without confirmation.** Nothing is destroyed — escrow returns to the balance and
goods to the hangar — so a misclick costs queue position, and a prompt on every withdrawal would be
noise on the common case.

`GET /market/my-orders?characterId=` answers with every open order, the station named, what is left
resting rather than what was placed, and what each is holding. A fourth overlay tab on `4` lists them
with a cancel per row and a footer naming the total locked.

**Worth knowing: a `Select` projection over a `Credits` property translates fine.** Only `Min`/`Max`
aggregation over one does not (see 109). The endpoint was tested for a 200 specifically because that
distinction is invisible at compile time.

**The list latched, exactly as the catalogue had.** `bRequestedMyOrders` was set once and never
cleared, so a newly placed order did not appear. That is the second instance of one bug — a bool
standing in for "have I asked yet" cannot tell "asked" from "asked, and the answer is now stale".
Opening the tab clears it and placing an order refetches. Worth grepping for the pattern before
adding a third.

## 120 — Start on foot, on the planet

**Done 18 August**, confirmed by playtest. Split out of 115 the same day.

`SpaceMMOGameMode` set `DefaultPawnClass = ASpaceMMOShipPawn`, so every connection spawned flying.
ADR-0012 says nobody starts with a ship; this is that sentence and only that sentence. Summoning,
active-ship state and the questline stay in 115.

The default pawn is now the character, spawned **deferred** and placed against the height function —
deferred because `ASpaceMMOCharacterPawn::BeginPlay` resolves the ground and aligns to it, so a
position set after `FinishSpawning` arrives too late and the first frame is spent somewhere else.

Placed above the surface and left to fall the last few metres, so ground contact catches the
character rather than the spawn positioning it by hand — which is what actually demonstrates that the
mesh and the height function agree. The drop clears `MaxElevationKilometres` as well as the radius,
or a character starts inside a hill.

A **starter ship** spawns 30 m away, unpossessed, for somebody to walk over and board. Scaffolding
until 115: flight is the most-tested thing in the project and losing casual access to it would be a
poor trade for a change about where a character stands. `bSpawnStarterShip` off is how "nobody starts
with a ship" gets tested before the questline that grants one exists.

### Two things this cost, both about where a fact lives

**The planet's position was written down twice.** The first attempt configured a starting centre on
the game mode, copied from the old `-SpawnCharacter` scaffolding, which said 200 km while the planet
had since moved to 60. The character spawned 121 km above the surface, resolved "up" perfectly
correctly against a planet it was nowhere near, and fell through empty space with nothing on screen.

**Then asking the planet actor did not work either**, because a connection is given its pawn *before*
`USpaceMMOWorldSubsystem::OnWorldBeginPlay` has spawned the planet — 323 ms apart, measured from the
log. The lookup found nothing, fell back to the placed transform, and dropped the character near the
system origin instead.

The planet's numbers were hardcoded inside the subsystem's spawn block, so they moved to
`USpaceMMOWorldSubsystem::StartingPlanet()` and `StartingPlanetTerrain()`. The subsystem builds the
planet from them and the game mode places a character from them: one definition, two readers, no
ordering to get wrong. Waiting for the actor and correcting afterwards would also have worked and
would have been a second mechanism for something that is a constant.

**The log line was part of the problem.** It printed a distance from a centre it had been handed, so
it read as correct while measuring from the wrong place — a diagnostic that agreed with the bug. It
now names the planet and radius it measured against, which is what turned the second failure into a
one-line diagnosis.

### Also: you could not look up or down

Found in the same playtest and not a regression — `DefaultInput.ini` bound `WalkTurn` to `MouseX` and
nothing at all to `MouseY`. Looking up and down had never existed on foot; starting there is simply
what made it obvious.

`WalkLook` on `MouseY`, inverted, clamped to ±85°. It pitches the **camera, not the character**:
turning belongs to the walk model because the server simulates which way a body faces, while pitch
changes nothing about where somebody stands — putting it in `FWalkInput` would replicate a number the
simulation cannot use and add a field to a struct with headless tests over it.

Both cameras get the pitch. The first-person camera hangs off the character root rather than the
boom, so tilting only the boom works in third person and silently does nothing in first.

**No automated coverage**, and that is not fixable cheaply: it needs a world, a game mode and a
possessed pawn, none of which the headless suite has. The log line is the instrument.

## 121 — The ground is untextured

**Done 19 August**, confirmed by playtest: banding by height, rock on the slopes, hills that read as
hills. Textures themselves are deferred — the blend is colours, and UV0 is in place for whenever a
texture wants tiling.

Both terrain meshes were assigned `/Engine/BasicShapes/BasicShapeMaterial` and neither builder wrote
UVs at all. So this was never "the material is wrong", it was "there has never been one".

### Decided: generated UVs, not triplanar

**I recommended triplanar and was wrong.** The globe is a cube-sphere already computing a `U` and a
`V` per vertex to place it, and discarding both. And LOD subdivides *within* a face, so a refined
tile's UVs are a sub-rectangle of its parent's — the parameterisation survives 122 by construction.
The error was conflating a cube-sphere with equirectangular mapping, which is the projection that
pinches at the poles. This project has never used that.

Triplanar keeps a narrow place on near-vertical ground, blended from the steepness channel rather
than replacing the base. Not built.

### What the meshes carry

`FPlanetTerrain::SurfaceUV(Direction)` gives cube-face coordinates in 0..1, and the globe and patch
both call it with the same direction — so the patch rim, where one hands over to the other, has no
seam. `MeshesCarryTheirUVs` fails if either builder invents its own.

Height and steepness go in the **vertex colour**, R and G. `ColorSpaceMode` defaults to `NoTransform`,
so 0..1 data passes through without an sRGB conversion mangling it.

**They were in UV1 first, and that did not work.** The mesh carried them correctly — 32768 triangles
across two layers, confirmed by the patch report — and the scene proxy forwards every layer it finds,
confirmed by reading `DynamicMeshSceneProxy.h`. A material reading `TexCoord[1]` still got a constant.
Rather than keep chasing where an index stops matching, vertex colour is the channel the engine and
every terrain material already agree on for blend weights, and it has no index to get wrong.

### The terrain had no slopes to shade

**The steepest ground on the planet was 5.9 degrees.** Relief was always half a kilometre and that was
never the problem: spread over `BaseFrequency = 2` it made broad swells. A material blending rock onto
cliffs had no cliffs, and one banding on height saw 0.31..0.37 across everything visible from the
ground. Both drew a flat colour, and neither was wrong.

Found by sweeping the parameter and measuring the built mesh — nothing about it was visible from the
configuration. `BaseFrequency = 12` gives 31.8 degrees and lifts the local height range to 0.37..0.78.
24 gives 47 degrees and 48 gives 70 if it ever reads too gentle. `HasSlopesToShade` fails if the
planet is flattened again.

### Two encodings that were correct and invisible

**Steepness was `1 - dot(normal, up)`.** A 32 degree hillside reads 0.15 that way, which is
indistinguishable from flat on screen. It is the sine of the slope angle now: the same hillside reads
0.53.

**Neither channel spans its range**, so the material remaps. Height occupies 0.37..0.78 and steepness
0..0.53, and a `SmoothStep` — 0.35..0.80 for height, 0.15..0.45 for steepness — is what turns a
correct-but-flat blend into visible ground. That belongs in the material rather than the mesh: the
ranges are per planet, and clamping them in C++ would throw away what a different world needs.

### Every planet looks different, and the data already says which

`data/universe/origin.json` holds five bodies and each has concept art with an explicit palette. **The
material belongs in `data/` per body**, on the same authority chain as everything else. Not built: the
first pass is one hardcoded look, agreed with Joe, so the shader could be got right without a seed
cycle on every tweak.

`TerrainMaterial` is a `Config` property on `ASpaceMMOPlanetActor`, so swapping a material needs no
rebuild — set it in `DefaultGame.ini`. It logs what it configured and what it loaded, on every path
including the one that does nothing, because a material that failed to arrive and code that never ran
otherwise leave identical evidence.

### The gap that made this expensive, now closed

**Nothing asserted that the ground kinds reached the mesh.** The builders were tested and correct
throughout; the step carrying their output onto the mesh was not tested at all, and it wrote height
and steepness into a channel materials read as a constant. Every measurement passed at every stage
and the ground was one flat colour.

The conversion is `FPlanetMeshAttributes::Write` now — its own class, because an `FDynamicMesh3`
needs no renderer and a test can therefore call it. `SpaceMMO.Terrain.GroundKindsReachTheMesh`
asserts height lands in red, steepness in green, and the surface coordinate in UV0, per corner, with
values chosen so a channel swap or a vertex mix-up cannot pass by coincidence. Verified by swapping
the two channels.

`PartialGroundKindsAreRefused` covers the other half: given fewer values than the mesh has vertices,
nothing is written. A partly filled overlay would blend toward whatever the missing elements
defaulted to and look deliberate.

### What textures and flora will actually cost, sketched 19 August

**Reasoning rather than a decision**, written down because it was asked after 96 landed — how do
textures, grass and flora reach the game, and is that this tool or something else? Neither, mostly,
and one rule decides it.

**The rule: does the server need to know it exists?** ADR-0002's line, not a new one. If it does, it
is content in `data/`, and 96's tool places it. If it is only ever looked at, it is client-side and
no content, no serving and no tool is involved at all.

**Textures are the cheap half, and this task already did the expensive part.** The material reads
height and steepness per vertex and blends three authored colours from a body's `appearance`.
Texturing means swapping those flat colours for samples driven by the *same two channels* — the
plumbing exists, is tested, and does not move. What might reasonably become content later is a
texture set per body, the way the colours already are: authored beside `appearance`, so what a world
is made of is stated where its palette is rather than compiled in. That is the whole change, and 96's
tool is not part of it, because it edits placement rather than appearance.

**Flora should never be content.** Nothing collides with grass, nothing gathers it, and the server
never adjudicates anything about it — so scatter it from the terrain function, seeded by direction,
and every client derives an identical field for free. That is exactly the argument ADR-0002 makes
about the ground itself. The hook already exists: the patch mesher rebuilds the ground under the
viewer as they walk, and scattering instanced meshes from the same directions at the same moment
gives stable flora with nothing replicated and nothing authored. UE's landscape grass system is not
available here and never will be — there is no landscape, the ground is a dynamic mesh.

**The exception is the whole point.** The moment a plant can be picked it stops being decoration and
becomes a deposit: content, in `data/`, placed with 96's tool, which is a thing that already works.
`verdant_amber` is gathered rather than mined precisely so one world plays differently from another.

**And the trap to refuse:** "gatherable plants everywhere, procedurally" needs C# and C++ to agree
bit for bit on where they are. ADR-0002 names that as the expensive thing to discover late, and
ADR-0011 already declined it once for caves. The cheap answer, and the one consistent with both, is
**authored gatherable points with procedural decoration scattered around them** — the player sees a
meadow, the server knows about four plants in it.

## 122 — One patch is not a planet

**Premise corrected and deferred, 19 August, on measurement.** Raised 18 August from prose in 84, 86
and `setup.md`; most of what that prose said had stopped being true.

### What was believed, and is not

**"One 1.4 km patch, with its rim a visible cliff."** The patch has not been a fixed size for some
time: `PatchDegreesForAltitude` sizes it to the viewer's horizon with a 1.2× margin, clamped between
4 and 60 degrees. At walking altitude it is about 1.4 km across; at the top of the atmosphere it is a
60 degree cap.

**"A small square on an otherwise featureless world."** The globe is hidden whenever a patch exists,
and the patch always reaches past the horizon — so in the atmosphere the globe is never visible, and
above it the patch is released and only the globe draws. **The two are never on screen together**, so
there is no rim to see and no seam to hide.

Both of those were true when written. Neither survived the patch becoming adaptive and
`SpaceMMO.HideGlobeUnderPatch` defaulting to 1.

### What is actually true, measured

The patch keeps a fixed 129×129 resolution while its area grows, so vertex spacing grows with
altitude — and the question that matters is how far the drawn mesh strays from the height function
the server resolves contact against:

| patch | vertex spacing | mean error | worst error |
|-------|---------------|------------|-------------|
| 4° (walking) | 21.8 m | 0.5 m | 2.7 m |
| 9° | 48.9 m | 1.1 m | 6.4 m |
| 20° | 106.9 m | 2.5 m | 14.7 m |
| 40° | 200.9 m | 4.8 m | 28.2 m |
| 60° (top of atmosphere) | 270.6 m | 6.7 m | 54.8 m |

**Half a metre where a player stands.** The error only grows where the viewer is kilometres away and
cannot resolve it, and it shrinks continuously on the way down — so a ship descending is never shown
smooth ground it then collides with. The patch at 60° (270 m spacing) also hands over to the globe
(327 m) at a similar resolution, so leaving the atmosphere should not pop.

### So this is deferred rather than built

Cube-sphere LOD remains the right long-term answer and is a large build. Nothing measured here
justifies it yet: the mesh is faithful where fidelity is perceivable, and the failure modes the task
was written about do not occur.

**Reopen it when one of these is true**, which are the things that would actually change the numbers:

- Terrain gains detail finer than about 20 m, which is where the 4° patch stops resolving it. Raising
  `BaseFrequency` past 12 or adding octaves does exactly that.
- A planet is authored much larger than 20 km, since spacing scales with radius.
- Somebody needs to see terrain detail from orbit — the globe samples every 327 m and always will.
- Caves (89) land, since an overhang cannot be expressed by either mesh at any resolution.

**Ruled out, and still ruled out:** tapering the patch rim down to the sphere. The mesh would then
disagree with the height function the server resolves contact against, and players would stand on
ground the server believes is elsewhere. `SpaceMMO.Patch.SitsOnTheTerrain` exists to catch that.

### Both belong to M7, which did not exist until this was written

**Agreed 18 August: M7 is "a world worth being in"** — 121, 122, 89 (caves), 96 (authoring) and 97
(settlements) — and careers, repeatable quest content and the repair loop moved to M8.

The roadmap went M6 combat, M7 depth, with caves and settlements filed under "depth" — which had
quietly made that milestone half about the world without saying so, while nothing anywhere named how
the world *looks*. M2 records terrain as done, true in the sense that it exists and false in every
sense a player would mean.

That is the same shape as the reconciliation failures already recorded in `README.md`, and it is now
the fourth one listed there.

## 123 — A planet's look and shape are content

**Done 19 August.** Split out of 121, which had deliberately hardcoded one planet's look to get the
shader right without a seed cycle on every tweak. This is that decision being paid back.

A body now carries both **what it is made of** and **what shape its ground is**, authored in
`data/universe/origin.json` and travelling the same path as everything else: `data/` → seed →
Postgres → API → client. Five worlds, five palettes, five silhouettes, and changing any of them is a
JSON edit and a re-seed rather than a rebuild.

**Palette and terrain are separate blocks on purpose.** A body may reasonably be painted before
anybody has decided how rugged it is, or shaped before anybody has chosen its colours, and either
alone is a working state. Both are optional; a body with neither keeps whatever the client was
configured with, which is what every planet did before this existed.

`ASpaceMMOPlanetActor::BodyKey` names which body a planet draws — config, so switching worlds is one
ini line. `USpaceMMOTerrainPaintSubsystem` does the applying, in the backend module, because Core
knows nothing about HTTP or content and keeping it that way is the same boundary the game mode holds
for its player controller.

### What this ran into

**Changing terrain has to drop the ground patch.** `SetTerrainConfig` rebuilt the globe and left the
patch alone, so the ground underfoot kept its old shape while the world beneath it changed — two
samplings of one height function disagreeing, which is the fault 86 exists to prevent. It now
forgets the patch so the next tick rebuilds it, and says so.

**Thresholds are coupled to terrain and the coupling is invisible.** The remap ranges that make
height and steepness usable were measured against Ares and copied to all five. Every body's terrain
puts those channels somewhere different: the Capital reaches 12 degrees at its steepest and Grimhold
46, so one shared slope range gave the Capital almost no rock and Grimhold nothing but.

Measured per body and set from measurement. `SpaceMMO.Terrain.BodyPalettesSuitTheirTerrain` reads the
authored JSON, builds real patches from each body's terrain, and fails when a body's thresholds fall
outside the ground it actually has — verified by flattening Grimhold and watching it fail on a
`heightTo` the ground no longer reaches.

**Contrast is a palette decision, not a threshold one.** Four of the five worlds were first authored
as tight tonal pairs, which is uniform by construction however well the blend works. They keep their
hue identity and gained value range; Ares was left alone, being the one that already read.

### Still hardcoded

**Radius.** Ares is 339 km in content and the planet drawn is 20 km. Making that real changes
approach times, orbital speeds and the horizon, and turns 0.5 km of relief from rugged into
Earth-scale subtlety — a flight change worth making deliberately rather than inheriting.

**Only one planet is spawned.** Five bodies are authored and one is drawn.

**Which body that is was two settings until 19 August, and they disagreed.** The planet actor took
`BodyKey` from `DefaultGame.ini` and drew `body_ares`; `USpaceMMODepositSubsystem` had a second
`BodyKey`, hard-coded to `body_capital` and not config at all, and placed that body's deposits and
stations. So the playtest world was Ares' terrain populated from the Capital, and neither setting
looked wrong from where it was written — the only evidence was `Placed 4 deposit(s) on body_capital`
in a client log next to a planet shaped like Ares.

Nothing about it was visible in play, either: the deposits stood on the ground correctly, because
placement asks the terrain function where the ground is and it answers for whatever planet it is
given. Content authored on the drawn body simply never appeared, and content authored on the
populated one appeared on terrain it was never placed against.

Found by building 96 and asking the obvious question — *which body do I author on to see it in a
playtest?* — which needed two files and a log to answer honestly.

**Now one key.** `USpaceMMODepositSubsystem::SceneBodyKey()` asks the planet in the world what it
is drawing and falls back to the configured default when it runs first. `DefaultGame.ini` names
`body_capital`, because that is where the onboarding chain lives: scrap to gather bare-handed, then
ferrite to mine once the laser is crafted. Ares has one level-15 ore behind a tool and nothing to
start on, which `SpaceMMO.Authoring.ConfiguredBodyIsPlayable` now asserts against whatever the key
names — verified by flipping it back to Ares and watching it go red.

The cost, accepted: the ground looks gentler. The Capital is frequency 6.0 against Ares' 12.0 and
0.35 km of relief against 0.5, and the terrain material was tuned against Ares' ruggedness (task
121). If it reads flat, the frequency is content and can be raised — 121 recorded 12 giving 31.8
degrees of slope and 24 giving 47.

## 96 — Author world content graphically

**Options 1 and 2 built, 19 August. Option 3 stays rejected.** Raised 11 August; ADR-0011 makes it
pressing, because a cave is a shape rather than a point and typing a shape into JSON by hand is
worse than typing a position — and shapes are the part option 2 does *not* yet answer, because
caves have no schema to author against until 89 starts.

**The constraint, and it is not negotiable:** `data/*.json` stays the source of truth. Content
reaches the game as `data/` → `--seed` → Postgres → C# API → HTTP → client, and the API is the
authority. It cannot read `.uasset` or `.umap`, so authoring *in* a UE level and leaving it there is
not an option — anything graphical has to write the JSON back out. Editing content in the editor and
forgetting the export would produce a world that looks right in the editor and does not exist in the
game, which is the worst failure mode available.

Three approaches, cheapest first:

1. **Capture in game. Built 19 August.** `P` prints the normalised direction from the nearest body's
   centre at the player's position, in the array shape `origin.json` uses, with the body key beside
   it — a direction means nothing without knowing which world it is on, and the key is what the
   content file wants next to it anyway. On screen as well as in the log, so the key visibly does
   something without alt-tabbing to find out.

   **It does not write to `data/`, deliberately.** A tool editing content while the game ran would be
   a second writer racing the seeder, and the export is the part that has to be right; copying one
   line out of a log is a smaller thing to get wrong.

   Confirmed the same day by authoring a ferrite deposit at the character's spawn point, which is
   the loop this replaces guessing unit vectors for. It does not help with shape, which is what 89
   will need.
2. **An editor utility that reads and writes `data/`. Built 19 August.** A new editor-only module,
   `client/Source/SpaceMMOAuthoring`, opened from **Tools → World Authoring**. It reads
   `origin.json`, draws the chosen body, stands a marker on every deposit and station on it, and
   writes back what moved. Deposits and stations only: a cave has no schema to author against until
   89 starts, and inventing one here would have been deciding 89 by accident.

   **It writes by splicing text, not by serialising.** This is the decision the rest of it hangs
   off. Nearly half of `origin.json` is `$comment` keys carrying the reasoning behind every
   placement, and `FJsonSerializer` would reorder keys, restyle every number and put all 324 lines
   into one diff — destroying the reasoning while looking like it worked. So a moved deposit
   rewrites the six numbers of its own direction and leaves every other byte alone.
   `MovingRewritesOnlyTheDirection` asserts exactly one line differs, which is the only version of
   that check that would notice.

   **The preview is a scale model at 500 m, not the planet.** Directions are scale-free, so any
   radius places identically — but not every radius *shows* the same thing. Half a kilometre of
   relief on Ares' authored 339 km is 0.15% of the radius: a smooth ball with nothing to place
   anything against. The game draws 20 km, where the same relief is 2.5%, so the preview shrinks
   both together and keeps the silhouette a player actually sees.
   `PreviewIsTheDrawnPlanetToScale` measures the ground in both models rather than trusting the
   settings, and fails if relief is ever scaled against the authored radius instead.

   **What it does:** move existing entries; add new deposits and stations, placed where the
   viewport camera is looking, with their fields edited in the marker's ordinary Details panel;
   and remove entries. Nothing reaches disk until *Write*, so *Discard* is simply rebuilding the
   preview from the file, and a session that ends in a crash has changed no content.

   **What it refuses to write**, naming the row: an empty or duplicated key, a deposit with no item
   or skill, an item or skill key that is not in `data/items/core.json` or `data/skills/core.json`,
   a planet-locked material that would end up on a second body (ADR-0008), a non-positive quantity,
   respawn, level or docking range, and a direction of zero. It also refuses if the file changed on
   disk since it was read, or if the result would not parse.

   **Two things found while building it, both worth keeping:**

   - **Unreal's JSON reader accepts a trailing comma and .NET's does not.** Removing the last entry
     of an array leaves the entry before it ending in one, and a test asserting "the result still
     parses" went green against that bug — the editor reads the file it wrote quite happily, and
     the C# seeder is the machine that matters. `HasDanglingComma` is now checked by the tests and
     by the panel before it saves. Found by deliberately introducing the bug and watching the test
     stay green, which is the only reason it was found at all.
   - **The labels rendered mirrored.** The text component's facing axis was pointed away from the
     camera rather than at it, and the default text material is two-sided — so a label facing away
     is not invisible, it is legible and backwards, which reads as a broken font rather than as a
     rotation. Settled by reading `TextRenderComponent.cpp:1118-1126`, which builds the glyph quads
     in the local YZ plane with the surface normal at +X: the component's +X must point at the eye.
     Found by playtest, because no headless run draws anything.
   - **Deleting a marker with the Delete key is not the same as removing an entry.** The actor goes
     away, so the write simply does not mention it and the deposit stays in the file — a deletion
     that silently does nothing. The panel now refuses to write when it finds a marker was deleted,
     and says to use *Remove selected* instead.
3. **Author in a level and export.** Rejected on the same grounds as above: two sources of truth,
   and the export is required regardless, so the level buys nothing.

Whatever is built, **re-seed after editing** — `dotnet run --project services/SpaceMMO.Api -- --seed`
is the only thing that applies changes under `data/`, and restarting the API does not. The panel
prints that line after every write for the same reason.

**Not verified by a headless run, and cannot be:** that the globe and the markers actually draw,
that dragging a marker snaps it to the ground, that the labels face the camera, and that the menu
entry appears. Everything the tool decides is tested — the parse, the splice, the removal, the
insertion, the comment preservation, the scale of the preview — but the editor half is looked at,
not asserted. See the checklist in the segment report.

**Renaming or removing an entry leaves an orphan in every database that already seeded it.** Found
20 August, on the first key this tool ever generated. `ContentLoader.UpsertResourceNodesAsync`
upserts by key and never deletes: a node whose key has changed is simply a *new* node, and the old
row stays, still servable, forever. The same is true of an entry removed entirely.

That is not new behaviour, and it is defensible — a seeder that deleted whatever was missing from
content would be one mis-pathed file away from emptying a production world. What is new is that this
tool makes renaming and removing one click each, so a trap that used to need a deliberate hand-edit
is now easy to spring. `node_ares_new_1` was authored, seeded, then renamed to
`node_ares_regolith_b`, and the database has both.

Consequences, in order of how much they matter:

- **A key is permanent the moment it is seeded, not the moment it is written.** Choose the real name
  before the first `--seed`, not after. The tool's generated `node_<body>_new_<n>` is a placeholder
  and should never survive to a seed.
- **Removing an entry hides it from nothing.** The row is still there and the API will still serve
  it. Removal is a content decision that needs a database change to finish, and neither this tool nor
  the seeder makes that change.
- Cleaning one up is a hand-written `delete from resource_nodes where key = '...'`, and
  `resource_node_states` restricts on delete, so any node a player has actually gathered from needs
  its state rows dealt with first.

**Not fixed here, deliberately**, because the options all have teeth: a prune step in the seeder is
the destructive one; a `retired` flag in content is a schema change; and a warning at seed time
naming rows that content no longer mentions is the cheap one and is probably right. That is worth its
own task when somebody has an opinion, and it belongs to whoever owns the seeder rather than to 96.

**It found a real bug in its first hour**, which is the argument for having built it: asking which
body to author against to see the result in a playtest turned out to need two files and a client log
to answer, because the drawn body and the populated body were separate settings that disagreed. See
123 — there is one key now.

**What 89 will need on top of this:** a cave is a volume, so it needs a shape gizmo rather than a
point marker, and a schema to write. The document layer is deliberately kind-agnostic — a third
`ESpaceMMOPlaceableKind` and a `FormatEntry` branch is most of what a cave entry would take — but
what a cave *is* in `data/` is 89's decision and is not pre-empted here.

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

### How a town is likely to be built, sketched 19 August

**Not a decision — reasoning, written down so it is not re-derived.** It came out of a question Joe
asked after 96 was built: how do custom assets, walls, fountains and crafting stations get into a
city? Three layers, and the split follows the rule ADR-0002 already set — *does the server need to
know it exists?*

1. **What the server must know is content**, in `data/`, placed with 96's tool: the station entries.
   Key, kind, direction, docking range. Crafting, storage and the market are station-scoped
   interactions and the spine already exists — `InventoryKind.StationHangar` is a per-station
   inventory carrying a `StationId`, and market orders are per station. A "bank" is close to free.
   `Housing` is an enum value with no behaviour behind it.
2. **What the server must never know is assets**: walls, a fountain, what the place looks like.
3. **The join is one anchor.** Five separately placed cubes will not read as a town. The likely shape
   is a prefab — one Blueprint or level instance holding the props — anchored to a *single* authored
   direction, with its interactive parts referencing station keys. `data/` owns where it is and what
   can be done there; the `.uasset` owns what it looks like. That stays one source of truth only as
   long as there is exactly one authored anchor: **the prefab must never carry a second opinion about
   position**, which is the same failure 96 exists to prevent, one level up.

**The hard part, and it is not the assets.** A prefab is flat and a planet is not. A town on a
hillside needs the ground levelled where it sits, which means the height field has to be overridden
inside an authored region — and the client and the dedicated server must agree about it, because a
player walks on it. That is ADR-0011's machinery pointed at a different problem, and it is why 89
should know this exists before it designs its lookup. See the note there.

**The cheap first step is 124**, which gives a station a mesh per kind and needs none of the above.

## 124 — A station is an engine cube, whatever kind it is

**Done 23 August.** Raised 19 August, out of the same question as the sketch in 97. Belongs to
**M7 — a world worth being in**, which already names settlements.

`USpaceMMOStationSettings` maps station kind to mesh and to size, with a per-key override, in
`DefaultGame.ini` — the deposit settings class copied deliberately rather than reinvented, down to
keying by the authored key rather than the database id and holding soft references.

**Sizes are per kind now**, which the single compiled constant could not express: Capital 40 m,
Spaceport 35, TradingHub 25, Social 12, Housing 8. The old value was 25 for everything, judged
against the horizon — on a 20 km planet that is about 260 m from eye height, so a 60 m building
subtends thirteen degrees and reads as the size of the visible world. Twenty-five metres of house
was the absurdity that came with one value for all five.

**Engine primitives as placeholders**, agreed with Joe: a cone for the Capital, a cylinder for the
Spaceport, a cube for the TradingHub, a sphere for Social, a small cube for Housing. Five
distinguishable silhouettes beat one cube five times, nothing needed authoring, and swapping in a
real building later is one line each. Housing and TradingHub share the cube and differ threefold in
size, which is honest rather than ideal.

**The scale is fitted to the mesh** rather than divided by the engine cube's known hundred
centimetres, which was right exactly as long as every station was that cube. `ScaleFitsTheMesh`
asserts the new arithmetic reproduces the old number for that case, so nothing changed size the day
it landed.

**Two things worth keeping:**

- **`EveryAuthoredKindHasALook` earned itself on its first run.** It reads the station kinds out of
  `origin.json` and asserts each has a mesh and a size configured, and it immediately failed —
  because the ini syntax was invented rather than copied. A `TMap` is written as one assignment with
  quoted values, exactly as `USpaceMMODepositSettings.Meshes` two sections above it had been doing
  all along; the `+Key=(("A", B))` form per line is for arrays and quietly does not populate the
  map. The proven example was ten lines up the same file.
- **A config class loads the ini into every instance it constructs**, not only into the class
  default. A test that built a fresh settings object and asserted "an unmapped kind resolves to
  nothing" was really asserting against whatever content happened to be shipped that week. The
  resolution-order tests empty the maps first, and say why.

**A kind may name a Blueprint instead of a mesh**, added 24 August when Joe bought a modular hangar
kit and asked how real art should reach the game. `BlueprintsByKind` and `BlueprintsByKey` hold a
`TSoftClassPtr<AActor>`, resolved before the mesh — where a kind has both, the mesh is the
placeholder somebody has now replaced.

**Why a Blueprint rather than a merged mesh.** A bought kit arrives as dozens of pieces with their
own materials, collision and LODs, and arranging them in a Blueprint keeps all of it while letting
the arrangement be edited without re-baking. It is also what 97 needs: a settlement is a prefab
holding props, and later a door or a vendor marker, none of which a static mesh can carry. Merging
into one mesh stays available as an optimisation for whichever stations turn out to be expensive to
draw.

**Carried on a `UChildActorComponent`**, not a hand-spawned actor, because it already solves the
three things doing it by hand gets wrong — spawning the class, attaching it, and destroying it with
the station. `Configure` runs before `FinishSpawning`, which is the worst possible moment to be
spawning something else.

**A Blueprint is drawn at the size it was authored** and never fitted. The component uses absolute
scale so the mesh path's fitting multiplier cannot reach it: a building should be the size it was
built, and multiplying that by a number chosen to fit an engine cube into twenty-five metres would
be a second opinion about how big a hangar is. `SizeMetresByKind` therefore applies only to the mesh
path, where it exists because engine primitives have no natural size at all.

**The path must end in `_C`.** That is the generated class rather than the asset, and without it the
load fails and the station quietly keeps its cube — the same trap as `CharacterAnimClass` in 125.
The warning names it explicitly for that reason.

**And the rule that matters more than any of it: the Blueprint must not know where it is.** Where a
station stands is content — a direction in `origin.json`, seeded, served over HTTP — and the class
only ever describes local geometry. A prefab that also remembered a world position would be a second
answer to "where is it", which is the failure 96 exists to prevent, one level up.

**What this does not do**, said plainly so it is not mistaken for the town: it puts a distinguishable
building where each station is. It does not lay out a settlement, does not place props, and does not
level the ground under one. Those are 97 and 89. Stations also still have no collision — contact is
`FPlanetTerrain` and docking is measured — so a hangar is scenery you walk through until somebody
decides interiors are real.

**What was true before this, verified and not inferred:** the client treated `kind` as an opaque
`FString` and used it in exactly one place — a log line at `SpaceMMOStationActor.cpp:122`. Every station in the game is the same
engine cube, scaled and lifted onto the terrain, whether it is a trading hub, a spaceport or
somebody's house. 97 already said this and it is still true: the enum, the JSON and the serving cost
almost nothing, and the entire visible difference is work that does not exist.

**This is a solved shape in this codebase, so solve it the same way.** Deposits had the identical
problem — one hard-coded engine cylinder for every material — and `USpaceMMODepositSettings` fixed
it: item key to soft mesh reference, in `DefaultGame.ini`, so a new ore is a settings entry rather
than a recompile and the mapping lands somewhere a diff will show it. Two details from that class are
worth copying rather than rediscovering:

- **Key by the authored key, never by the database id.** Ids are assigned by whichever database
  seeded last; a mapping keyed by id points at the wrong building the first time the database is
  rebuilt in a different order.
- **Soft references**, so a world with one station does not pay to load every model in the
  catalogue.

Keyed by `StationKind` first — there are five and they are what a look would follow — with an
optional per-key override, so one named station can differ without inventing a kind for it. That
override is also what a settlement's anchor would use later.

`FDepositPlacement::UniformScale` and `BaseLift` already absorb the two things that differ between
any two models somebody exports — how large it was authored, and where its pivot sits — as pure
statics with tests. A station wants exactly the same arithmetic at a different target size, so this
is a reuse rather than a rewrite.

**What this does not do**, said plainly so it is not mistaken for the town: it puts a distinguishable
building where each station is. It does not lay out a settlement, does not place props, and does not
level the ground under one.

## 125 — The character is a tube

**Done 22 August**, confirmed by playtest: a rigged human runs, jumps and falls on the planet, turns
to face where it is going, and is hidden in first person. Raised 20 August by Joe, wanting the game
to look like a game. Belongs to **M7 — a world worth being in**.

**Built so far: the pawn can wear a model.** A `USkeletalMeshComponent` on
`ASpaceMMOCharacterPawn`, with the model, the animation blueprint and how the mesh sits on the pawn
all read from `DefaultGame.ini` — the same reasoning as the terrain material and the deposit meshes:
deciding how something looks means trying a value and looking at it, and a rebuild in the middle of
that loop is how people stop iterating. The placeholder cylinder stays as a fallback when nothing is
configured or the model fails to load, for the reason the deposit settings give: an invisible
character reads as the player not existing, which is far worse than an ugly one.

**Two things that made this much cheaper than expected, both verified rather than assumed:**

- **The rig uses UE5 mannequin bone names** — `pelvis`, `spine_01`..`spine_05`, `thigh_/calf_/foot_`,
  five spine bones and not four. Read out of the imported skeleton asset rather than guessed. So the
  mesh could simply be bound to the skeleton the animation library already uses, and no retargeting
  was needed at all. Confirmed working by Joe the same day: the library's animations preview on the
  character.
- **There are two `SK_Mannequin` skeletons in the project**, one under `Characters/Mannequins` and
  one under `FreeAnimationLibrary/Demo`, and the animations are bound to the second. Binding to the
  wrong twin would have left everything looking correct with no animation playable. Found by reading
  which skeleton `anim_Jog_Loop_Fwd` actually references.

Assigning the skeleton added the rig's face bones (`c_eye_*`, `eyelid_*`, `c_jawbone_x`) to the
library's `SK_Mannequin`, which is a tracked vendor asset. Nothing animates them, so they hold their
bind pose; re-importing that library would drop them and need the dialog answering again.

**What the animation blueprint has to read, and it is all published now:**
`GetGroundSpeedMetresPerSecond`, `GetMoveDirectionDegrees`, `GetVerticalSpeedMetresPerSecond` and
`IsOnGround`, all `BlueprintPure` on the pawn and all backed by pure statics on
`FCharacterWalkModel` with headless tests. Three details in there are the ones worth not
rediscovering:

- **Ground speed is not speed.** A character stepping off a ledge is moving fast and walking
  nowhere; a blend space driven by total speed sprints harder the further it falls.
- **Direction is measured against the surface normal, never world Z.** The same lesson the walk
  model, the camera and the terrain material each learned separately. `MoveDirectionIsLocal` checks
  all four headings twice — once at a pole where up happens to be world Z, once on the equator where
  it is not — and was verified by substituting world up and watching only the equator case fail.
- **Vertical speed keeps its sign**, because that is what tells a jump from a fall, and they are
  different animations.

Animation is drawn, never simulated: the server owns where a person is, and no pose may argue with
it. Nothing here uses root motion, and every value an animation blueprint reads is filled on remote
pawns too — `FollowServerState` writes the same walk state from what the server replicated, so other
players animate rather than sliding about in a bind pose. Checked by reading it, before it could
become a playtest.

**How it was finished:**

1. The animation blueprint. **Specified 20 August**, below, and built 22 August.

   Create it on the **FreeAnimationLibrary `SK_Mannequin`** — the skeleton the mesh was bound to,
   not the `Characters/Mannequins` twin — at `/Game/Characters/Human/ABP_Human`. Set **Root Motion
   Mode to Ignore Root Motion** in its class defaults: the server owns where a character is, and
   root motion is how a pose gets to argue with it.

   **It is `Ignore Root Motion`, not `No Root Motion Extraction`**, and this spec said the wrong one
   for a day. The engine's own comments settle it — `NoRootMotionExtraction` is "leave root motion in
   animation", which keeps the root translation in the pose and walks the mesh away from the actor;
   `IgnoreRootMotion` is "extract root motion but do not apply it", which is the one that holds the
   mesh on the character. The symptom of getting it wrong is a character that drifts off the middle
   of the screen while moving, and a backward animation that strikes its pose and then slides.

   **`BS_Human_Locomotion`**, a blend space on the same skeleton. Horizontal axis `Direction`,
   -180..180, 4 divisions. Vertical axis `Speed`, 0..6, 3 divisions — metres per second, matching
   what the pawn publishes, with the jog row at the 6 m/s the walk config caps at.

   | Speed | -180 | -90 | 0 | +90 | +180 |
   |---|---|---|---|---|---|
   | 0 | `anim_Idle` | `anim_Idle` | `anim_Idle` | `anim_Idle` | `anim_Idle` |
   | 2 | `anim_Walk_Bwd_Loop_L` | `anim_Walk_Left_Loop` | `anim_Walk_Fwd_Loop_L` | `anim_Walk_Right_Loop` | `anim_Walk_Bwd_Loop_L` |
   | 6 | `anim_Jog_Loop_Bwd` | `anim_Jog_Loop_Left_L` | `anim_Jog_Loop_Fwd` | `anim_Jog_Loop_Right_L` | `anim_Jog_Loop_Bwd` |

   Both ±180 columns hold the same backward asset so the blend has somewhere to go when direction
   wraps. Every sample is an `_L` variant where there is a choice: those suffixes are which foot
   leads, and mixing them inside one blend space is foot sliding wherever the blend crosses between
   them.

   **Event graph**, on Event Blueprint Update Animation: `Try Get Pawn Owner`, cast to
   `SpaceMMOCharacterPawn`, and read `Speed` from `GetGroundSpeedMetresPerSecond`, `Direction` from
   `GetMoveDirectionDegrees`, `VerticalSpeed` from `GetVerticalSpeedMetresPerSecond` and
   `IsGrounded` from `IsOnGround`. A failed cast leaves them zero and the character idles, rather
   than erroring every frame.

   **State machine**: `Grounded` playing the blend space, `JumpStart` playing
   `anim_InPlace_Jump_L` once, `Falling` looping `anim_FallLoop_01_L`.

   | From → To | Condition | Blend |
   |---|---|---|
   | Grounded → JumpStart | `NOT IsGrounded AND VerticalSpeed > 0.1` | 0.05 |
   | Grounded → Falling | `NOT IsGrounded AND VerticalSpeed <= 0.1` | 0.20 |
   | JumpStart → Falling | `VerticalSpeed <= 0.0` | 0.15 |
   | JumpStart → Grounded | `IsGrounded` | 0.10 |
   | Falling → Grounded | `IsGrounded` | 0.15 |

   The sign of vertical speed is what separates jumping from walking off a ledge, which is why it is
   published with its sign rather than as a magnitude. `JumpStart → Grounded` looks redundant and is
   not: a jump that lands before its takeoff animation finishes has nowhere else to go, and stays
   stuck in the pose.

   `anim_LandRoll_R` is deliberately unused. It is a full roll and reads as absurd after a small
   hop; it is worth a fourth state later, gated on how hard the landing was, which is vertical speed
   at the moment `IsOnGround` becomes true.

   Then `CharacterAnimClass=/Game/Characters/Human/ABP_Human.ABP_Human_C` in `DefaultGame.ini`.
   **The `_C` matters** — that is the generated class rather than the asset, and without it the load
   fails and the character stands in its bind pose, which the log says out loud.

   **What was built differs from that spec in one way**, and the reason is worth keeping: the blend
   space is one-dimensional, on speed alone — `anim_Idle`, `anim_Walk_Fwd_Loop_L`,
   `anim_Jog_Loop_Fwd` — because the body now turns to face travel, and a character who faces where
   they are going only ever runs forward. See below.
2. Judge the model on the planet: whether `CharacterMeshRotation` is right, whether it stands on the
   ground rather than in it, and whether the third-person boom still frames a person rather than a
   cylinder. All three are config or a number, not a rebuild. **All three passed 20 August**, after
   the height fix below.

   **Done 20 August, and it found one.** Facing and footing were right first time; the character was
   half size. It read on screen as the ore deposit being enormous, and the log settled it in one
   line rather than by argument — the standing-gap diagnostic, changed the same day to measure
   whichever body is drawn, reported the model spanning 0..98 cm. A deposit is fitted to a 300 cm
   box, and 300 ÷ 98 is exactly the ratio on screen.

   Fixed the way deposits already solve it: `CharacterHeightCentimetres`, default 180, with the mesh
   uniformly scaled to stand that tall and the log naming the authored height as well as the applied
   scale — so a model exported at the wrong size stays visible as a fact rather than being silently
   corrected forever. 180 because everything else on the pawn was built around it: the placeholder
   tube was 180, and the two cameras sit at 160 and 165, eye height on a person that tall.

   **The cheap fix, not the correct one**, and it is worth saying which. Anything later attached to a
   socket — a mining laser in a hand — inherits the multiplier and has to remember it. Re-exporting
   the model at human scale and setting the height to zero is the version with one authority instead
   of two.
3. **First person hides the whole body**, agreed with Joe rather than hiding only the head.
   Confirmed by playtest, 20 August.

**Left undone deliberately:** `anim_LandRoll_R` is still unused, because a full roll after a small
hop reads as absurd; it wants a fourth state gated on how hard the landing was, which is vertical
speed at the moment `IsOnGround` becomes true. The lateral and backward jog clips are unused for the
reason below. And the model is still scaled 1.836 at runtime rather than exported at human scale,
which is fine until something is attached to a hand socket and inherits the multiplier.

### The clips are angled runs, not strafes, so the body turns instead

The library's `Jog_Loop_Left_L` and its siblings are not what their names suggest: previewing one
shows the whole upper body rotated away and the head turned, rather than a torso square to the
camera with the feet crossing over. In game that read as *running left while facing right*, which is
neither a strafe nor a turn.

Decided with Joe, 22 August: **the drawn body turns to face the direction of travel**, and the
lateral clips are not used at all.

**Mesh only. Nothing the server simulates changed.** The pawn still faces the mouse and still
strafes; `bCharacterFacesTravel` swings the skeletal mesh yaw toward the travel direction and does
nothing else. The walk model stays pure, tested, and identical on the dedicated server — which is
the reason it was done this way rather than by making the character genuinely rotate. That version
needs a rotate-toward-heading behaviour inside the replicated simulation and a camera decoupled from
the pawn, and it changes how the game plays rather than how it looks.

It also collapsed two problems rather than solving them. With the body facing travel the blend space
needs one dimension and three samples, so the misnamed lateral clips and the backward clip that
would not animate are simply never played.

`TurnTowards` is a pure static with tests, and the wrap is the test that matters: turning from 170
degrees to -170 is a twenty degree step, not a three hundred and forty degree spin, and running
backwards flips that sign on numerical noise every frame. Verified by substituting the naive
difference and watching it go red. The same discontinuity had already frozen the blend space earlier
the same day, which is how it earned a test here.

Turn rate, threshold and the behaviour itself are config, because 720 degrees per second is a feel
value and feel is judged by looking.

### Root motion reached the pose, and only `Force Root Lock` stopped it

**Four wrong answers before the right one**, all recorded because each looked correct at the time.

The symptom: the drawn body ran ahead of the actor by up to seven metres, snapped back, and did it
again. `SpaceMMO.LogCharacterDraw` measured it as a clean sawtooth once it was pointed at the right
object.

- **`No Root Motion Extraction` was wrong**, and this task's own spec had said it. The engine
  comment is "leave root motion in animation", which is the opposite of what was wanted.
- **`Ignore Root Motion` did not fix it**, though the mode was confirmed applied by reading it off
  the running instance rather than the saved asset.
- **`Root Motion From Everything` did not fix it either.**
- **`Enable Root Motion` was unchecked on every clip**, which is what made all three irrelevant.
  `AnimationDecompression.cpp:274` locks the root only when
  `(bExtractRootMotion && bEnableRootMotion) || bForceRootLock`. With the middle term false no
  instance mode can satisfy the left half, and `bForceRootLock` is the only term left — its own
  engine comment reads "Force Root Bone Lock even if Root Motion is not enabled".

**The fix is `Force Root Lock` ticked on every clip**, done in one pass through Asset Actions → Edit
Selection in Property Matrix. `Enable Root Motion` stays off: it means "this clip drives the
character", and movement here belongs to the server.

An earlier reading claimed root motion *was* enabled on those clips. That was wrong and worth
naming: the asset binaries were grepped for the property name and its presence reported as the value
being true. A name in an asset table is not a value. It was an inference presented as a measurement,
which is the same mistake as the one below.

### "The character is not centred while moving" — measured, 20 August

Reported from a screenshot with the character hard against the left edge of the screen. **Three
causes were proposed and all three were wrong**, which is the point at which this project's own rule
says to stop arguing and print numbers.

Wrong guess one: the root motion mode, which was genuinely wrong in the spec — `IgnoreRootMotion`
rather than `NoRootMotionExtraction` — but changing it did not fix the symptom. Wrong guess two:
that the running session had never picked the change up, which was true of that session and still
did not fix it on a clean one. Wrong guess three: an instrument that measured the angle from the
camera to the *actor origin*, which is the character's feet, and duly reported a constant 20 degrees
off centre. That is `atan(160/430)` — the geometry of a camera at neck height looking level — and it
was a number the diagnostic invented rather than found.

`SpaceMMO.LogCharacterDraw` is what settled it, once it measured the character's middle, split the
offset into the camera's own sideways and vertical axes, and sampled every frame rather than once a
second. Across 47 seconds of deliberately hard turns:

- **Sideways: 0.0 to 1.6 degrees, never more.** The character is horizontally centred at all times.
- **Vertical: 9.7 degrees, steady**, which is the middle of a 180 cm character seen from a camera
  400 cm back and 160 cm up. Expected.
- **Vertical spikes to 38 degrees**, which are mouse-look: `LookUp` pitches the camera boom, so
  looking up and down swings the character in frame. Also expected.

So the original screenshot was the root motion leak — a pose dragging the mesh in the direction of
travel — and it is fixed. What remains is the framing: the character sits slightly low because the
camera looks level from neck height, which is a tuning preference rather than a fault.

**Two things kept from this**, both about the diagnostic rather than the bug:

- A once-a-second sample cannot see a transient. The first version reported the same steady number
  for a character that swung wide and came back as for one that never moved.
- **A console variable resets every run**, so a run where nobody set it writes no lines at all — and
  a log with no diagnostic in it looks exactly like a run where nothing was wrong. That cost a whole
  round trip. The switch now says whether it is on or off at startup, so "off" is a fact in the log
  rather than an absence from it.
- **It measured the actor, which is bolted to the camera boom**, and so reported zero degrees off
  centre whatever was on screen. It proved the actor was centred, which was never the question: a
  player looks at the skinned mesh, and a pose can walk that seven metres away while the actor it
  hangs from does not move at all. It reads the pelvis bone in world space now. Two rounds were
  spent on a number that could not have detected the fault.

**Not fixed, and worth knowing:** the rig carries Auto-Rig Pro controller bones (`c_` prefix) and a
stray `OBroot`, which are Blender-side scaffolding rather than anything the game needs to evaluate.
Re-exporting with deform bones only would make the skeleton leaner. It costs nothing today, so it is
recorded rather than done.

## 126 — The starter world was a quarter of a planet away from the spawn

**Done 24 August**, found while trying to look at a station up close and measured rather than
argued about. Belongs to **M7 — a world worth being in**.

`station_capital_hub` and all four starter deposits sat at direction `[-1, 0, 0]` while a character
spawns at `[0, 0, 1]` — **exactly 90 degrees apart, which on the drawn 20 km planet is 31.4 km of
walking**. The only thing at the spawn was `node_test_player_spawn_ferrite_a`, the deposit authored
with the capture key when 96's first option was built.

So the onboarding chain read: arrive, find one ferrite deposit you cannot mine without a laser you
have not crafted, and walk 31 km to reach the scrap that step one actually asks for.

**Nobody moved the content. The spawn moved.** Task 120 put players on the ground at `+Z` on 18
August, and the placement it landed next to was never revisited. Every comment in `origin.json` went
on describing the old arrangement, including one that said in as many words:

> A new player should not have to circumnavigate a planet to find step one, and a deposit they
> cannot find is indistinguishable from one that does not exist.

That sentence was an intention that had quietly become false, which is the specific failure this
file's own rules keep warning about — a note that describes what somebody meant rather than what is
true, sitting next to content nobody re-measured.

**Fixed by rotating the cluster, not by re-placing it.** A quarter turn about Y — `(x, y, z)` to
`(z, y, -x)` — maps `-X` onto `+Z` and preserves every deposit's position relative to its
neighbours, so the layout somebody chose is intact and only the face changed. Then the whole cluster
is nudged 1.5 degrees, about 520 m, so a 25 m building is not standing on the arrival point.

| | Was | Now |
|---|---|---|
| `station_capital_hub` | 31.4 km | 0.52 km |
| `node_capital_ferrite_a` | 31.4 km | 0.66 km |
| `node_capital_ferrite_b` | 30.9 km | 1.06 km |
| `node_capital_scrap_a` | 31.7 km | 0.85 km |
| `node_capital_scrap_b` | 31.8 km | 0.65 km |

Five changed lines, all `direction`, every comment intact — the same property 96's tool guarantees,
because it is the same operation. The stale comments were rewritten to say where things are now and
to record that the old claim had been false for weeks. Verified in Postgres after seeding rather
than assumed from the file.

**Done by arithmetic rather than with 96's tool, deliberately.** Dragging is for "put it where it
looks good"; rotating a cluster 90 degrees while preserving its internal layout is a computation,
and doing it by hand would have lost the relative spacing that was authored on purpose.

**A test failed on the comment rewrite, and it was right to.**
`SpaceMMO.Authoring.MovingRewritesOnlyTheDirection` proved the splice does not eat comments by
asserting a literal sentence from `origin.json` — so rewording the content turned it red. The
property was right and the check was brittle in the same way asserting counts of shipped content is.
It now reads the entry's comment out of the file first and asserts *that* survives, whatever it
says. Verified by making the splice eat sixty characters of the line above and watching it, and
`CommentsSurviveEveryEdit`, both go red.

**What this does not settle:** whether `+Z` is the right place to arrive at all. The deposits'
original comments talked about the face a ship approaching from the system origin reaches, which is
a real consideration once landing somewhere other than the spawn is a thing anybody does. This moved
the content to the players rather than deciding where players should be.

## 127 — A-02 Capital Hub, greyboxed from the plan set

**Done 25 August.** Belongs to **M7 — a world worth being in**, alongside 97 and 124. Nothing in
the game references it yet; this is the drawing turned into geometry so it can be argued with.

`tools/greybox/a02_capital_hub.py` builds sheet A-02 in Blender headless, writing a `.blend`, an
FBX and six preview renders into whatever `--out` names. Rerunning it is the only way any of them
should ever change — the numbers live in the script, not in the artefacts.

**Only the FBX is kept here**, in `client/RawContent/Stations/A02_CapitalHub/`, because it is what
the engine consumes. The `.blend` and the renders live in Joe's Blender directory outside this
repository, on the same reasoning as a83ed80: a bought kit and a modelling scratch file are raw
material, not a dependency of the build. Nothing is lost by that as long as the script is here,
which is the point of the script being here.

**The scale came off the sheet's own dimension line**, not off a guess: A-02's 40 m dimension spans
SVG x=30..430, so 10 SVG units = 1 metre, and every wall centreline, door gap and service rectangle
in the file is read from the drawn paths at that scale. The 6 m main entrance and the 8.0 x 2.5 m
supply counter both fall out of it exactly, which is what says the scale is right.

**The script asserts the model against the sheet's printed room schedule** before it builds
anything — trade floor 552, offices 320, hangar 184, departures 184, cores 216, arrival hall 144,
gross 1600, eight terminals, six alcoves. A mis-transcribed coordinate fails the run rather than
turning up in a screenshot days later. This is the same discipline as
`SpaceMMO.Terrain.HasSlopesToShade`: measure the artefact, not the thing that configures it.

**The plan gives widths and areas, and no heights at all.** Door heads (3 m, 3.5 m at the two
entrances, 4 m at the arrival opening) and every service element's height are the greybox's
invention, flagged in the script. So is the Level 01 floor at 4.5 m, which is the only reading that
makes note 6's "9 m over the trade floor, 4 m elsewhere" close.

**Two things the plan does not settle, both left as drawn and noted:**

- **The alcove fronts.** The sheet draws a partition clean across each of the six alcoves with no
  door gap, which would seal them. Built as a 1.1 m counter, because an alcove you cannot enter is
  not an alcove and a career giver stands behind a counter. One constant to change.
- **The Level 01 south core wall is 0.8 m south of the Level 00 wall below it** (y=31.8 against
  y=31.0). Drawn that way, kept that way. It reads as a drafting tolerance rather than intent, but
  the greybox shows the offset rather than quietly correcting it.

**Superseded before it was built, on purpose.** A-02 note 5 and the plan set's build order both say
A-07 Borlash City replaces this on `body_capital` and that A-02 is deferred indefinitely. It was
greyboxed anyway as the pattern for a large hub, and because a 40 m two-level shell is the thing
R2 is about — see below.

**Blocked-on, and what it does not touch:** nothing here changes `origin.json`, `DefaultGame.ini`
or any Blueprint. It was blocked on R2 of the plan set — the capital was authored as a
`TradingHub`, so the 40 m footprint this sheet is drawn to was not the one the game used. **Fixed
the same day in 128**, which is what makes this sheet's dimensions and the game's agree.

**Amended 26 August: it flickered, and the reason was that every junction met exactly.** Joe opened
the `.blend` and reported meshes fighting, worst around Level 01. The first build put wall tops on
the slab top at 4.5, cut slab edges to the wall faces they abutted, and stood everything on the
surface it sat on. All of that is correct, and all of it is unrenderable: two coplanar faces facing
the same way at the same depth have nothing to break the tie.

**Measured rather than argued.** `report_coincident_faces` walks every pair of solids and reports
same-facing coplanar faces with overlapping area. It found **155 pairs**, the worst 20 m², and the
top of the list was `GB_L00_Walls vs GB_L01_Slab` at `z=4.5` — which is exactly the floor Joe named.
Reasoning about it from the source would have been guessing; the numbers named the plane.

**The rule now is that solids overlap rather than touch**, by a `KNIT` of 0.1 m. Wall tops die
inside the slab, slab edges die inside the walls, the roof soffit dips below every wall top, and
anything standing on a floor starts below its surface. Nothing looks different, because a face
buried in another solid was never visible.

**Two of the 155 were not knitting problems but modelling ones**, and both are worth keeping:

- **The 8 m opening had a lintel that could not exist.** Its head is at 4.0 m, which *is* the slab
  soffit, so the lintel was a 0.3 m sliver whose underside lay in the ceiling plane. The slab is
  the head. Deleted.
- **25 treads, not 26.** The stair drew a final tread whose top face was the Level 01 floor it
  landed on. The slab is the twenty-sixth tread, and the last riser is the step onto it.

**The check now grades by material, which is the honest measure.** 172 coincidences remain and are
deliberate: same-facing coplanar faces where *both* solids carry the same material shade
identically, so the depth test ties without flickering — butt joints between two wall segments are
the common case. 132 more are sealed inside a third solid and cannot be seen at all. What the run
now asserts is **zero coincidences across two materials**, which is the set that actually shows.

**The roof was briefly given a 0.3 m oversail** to dodge a coincidence with the perimeter face,
which quietly grew the envelope to 41.1 m. Reverted to an inset edge: fixing a rendering fault is
not licence to change the building, and the sheet says 40.5 m over the walls.

---

## 128 — StationKind.Capital was configured and authored on nothing

**Done 25 August**, from R2 of the Origin Station Plans, which 127 named as the item blocking any
of that greybox reaching a station. Belongs to **M7 — a world worth being in**.

`DefaultGame.ini` has given `Capital` a cone at 40 m since 124. `origin.json` authored
`station_capital_hub` as `kind: TradingHub`, so **the Capital entry had never once been exercised**
and the capital drew the same 25 m cube as every outpost on every homeworld. It had looked
different for a while, which is what hid this: `BlueprintsByKey` pointed that one key at the bought
kit's example building. a83ed80 commented that line out for being a dangling reference in a fresh
clone, and the capital went back to being a cube with nothing anywhere saying it should not be.

**One word, as R2 said**: `"kind": "Capital"`, plus a re-seed. Verified in Postgres rather than
inferred from the seed's success line — `stations` now reads `station_capital_hub | Capital`, and
with no `MeshesByKey` entry and `BlueprintsByKey` commented out it resolves to the cone at 40 m.

**Why the existing test could not catch it, and why the obvious new test would be wrong.**
`EveryAuthoredKindHasALook` reads the kinds out of `origin.json` and asserts each has a mesh and a
size configured. It checks that *authored* kinds have a look, not that *configured* kinds are
authored, so a kind sitting in the ini with nothing pointing at it is invisible to it. Inverting it
— assert every configured kind is authored — was considered and rejected: `Social` and `Housing`
are configured today and authored by nothing on purpose, ahead of the content that will use them,
and a test that went red for them would be reporting the config working as intended. The actual
fault was narrower and is not generically testable: a station called "Capital Trading Hub", sitting
on `body_capital`, was not of kind `Capital`. Nothing but reading it catches that.

**Nothing branches on kind**, which is R9 of the same plan set and is why this is a content change
and not a behavioural one. Every `StationKind` reference outside the enum is a test fixture
building its own station. Quest 7 targets the capital by key in `main-story.json`, not by kind, so
`intro_fly_to_capital` is untouched.

**The direction comment was stale in the same edit.** It justified the placement as "far enough
that a 25 m building is not standing on top of the arrival point"; the building is 40 m now. The
500 m still holds, and 40 m is visible from further away than 25 m was, so the placement did not
need to move — only the sentence explaining it.

## 129 — A station stood on the wrong ground, about half the time

**Done 26 August**, found by playtest: the same build put the A-02 capital hub on the ground one
run and roughly a hundred metres above it the next, with nothing changed between them. Belongs to
**M7 — a world worth being in**.

**A race, and the log timings prove which one.** `FetchBodies()` and `FetchStations()` go out in
parallel at world begin play. Two world subsystems then subscribe to the *same* `OnBodiesLoaded`
broadcast:

- `USpaceMMOTerrainPaintSubsystem` reshapes the planet from that body's authored terrain — the
  capital is seed 20260805, relief 0.35 km, frequency 6.0.
- `USpaceMMODepositSubsystem` places stations, reading `PlanetActor->GetTerrainConfig()` at that
  instant.

When the stations response lands first, placement happens *inside* the bodies broadcast and races
the reshape. Lose it and the station is positioned against the compiled-in default terrain — seed
20260801, relief 0.5, frequency 12, from `USpaceMMOWorldSubsystem::StartingPlanetTerrain` — and
then the ground is reshaped underneath it and never re-derived. The difference between two height
fields at one direction is the hundred metres. Win it and everything lines up. Nothing in between,
which is why it flipped cleanly rather than drifting.

**The gate had two of its three inputs.** `PlaceStationsWhenReady` already existed and its comment
reasons carefully about stations and bodies arriving in either order — that ordering had bitten
before and been fixed. Nobody added the third thing placement depends on: the shape of the ground it
is placing against.

`USpaceMMOTerrainPaintSubsystem` now broadcasts `OnPlanetsPainted` once the planets have the shape
they will keep, and the gate waits for it. Two details carry their own comments because both are
ways this fix could have been written wrong:

- **The signal fires even when a body has no authored terrain to apply.** A body nobody has shaped
  is a working state, and a gate waiting for a signal that only fires on the interesting path
  deadlocks into a world with no stations in it and nothing in the log about why.
- **The speculative paint at world begin play does not count as settled.** It runs before anything
  has arrived, and treating "nothing to do yet" as "settled" reopens exactly the race being closed.

**Verified by the thing that found it**, because a race cannot be reproduced headlessly. Across
restarts the log now shows the settle line before placement every time, and two different gaps —
1763 ms on the ordering where the ground settled first and stations arrived later, 3 ms on the
ordering where they were already waiting and the paint released them. Both orderings, both correct.

**Deposits were never affected, but only by accident.** `FetchDeposits` is issued *from* the bodies
handler, so its response cannot arrive until that broadcast has finished and the terrain is settled.
That is safety by ordering rather than by statement, and it breaks silently if anyone ever moves
that fetch earlier.

### The diagnostic that could not see it, which is the part worth keeping

A station's placement was already logged as "placed 0.0 m above the ground at its direction", added
the day before to settle whether a floating building was a pivot bug or terrain. It read zero on
every run, including the broken ones.

It compared the station's position against **the terrain config the station itself was holding** —
the same stale one it had been placed with. Perfectly self-consistent, and structurally incapable of
detecting this fault: the station and its own copy of the ground agreed exactly, while both
disagreed with the planet everybody could see.

That was the third instrument in two days to measure a thing against itself rather than against what
it is supposed to agree with. The others were an off-centre check that measured the actor — which is
bolted to the camera boom and therefore always centred — rather than the skinned mesh a player looks
at, and a standing-gap check that measured the character's feet, which are below the camera's aim by
construction. Each read a confident number that could not have been wrong, and each sent somebody
looking in the wrong place.

**So placement now names the terrain seed it used.** If a station is ever placed against 20260801
while the capital is 20260805, that line says so outright, and no reasoning about pivots is needed.

## 130 — Things you cannot walk through

**Done 30 August**, bar the roof — see task 133 for why that one asset is a re-import nobody needs to
do today. Belongs to **M7 — a world worth being in**, and implements
[ADR-0013](adr/0013-terrain-is-a-function-everything-else-collides.md), accepted 26 August: terrain
stays a pure height function with no collision geometry at all, and everything standing on it —
ships, deposits, buildings — gets ordinary Unreal collision, queried rather than simulated.

**Query-only, never simulated,** for the same reason the planet has none: where a ship is comes from
the flight model and the server, and a physics body would be a second opinion about that. The pawn
owns no body either. It sweeps a capsule from where it was to where the walk model wants it and
resolves the hit itself, so there is no accumulated solver state for a render-origin rebase to
disturb (ADR-0001).

The seam is deliberate. Whether something is in the way is a question about the world and lives in
the pawn; what happens once the answer is yes is movement and lives in `FCharacterWalkModel`, where
it is tested with no world at all.

### Done

Ships and deposits are `QueryOnly`, `ECC_Pawn` blocked and everything else ignored, and the
character sweeps against them.

### Deposits are not solid, and the mesh is the reason, not the code

**Fixed by authoring, not by code.** `Ferrite_Ore` and `Scrap_Deposits` carried no simple collision,
so nothing could hit them. Convex hulls added in the Static Mesh editor; both assets now hold
`KConvexElem` data, and a playtest confirms a character stops against a deposit and slides off it.

The sweep leaves `bTraceComplex` false, and the engine reads that flag as a choice of exactly one
kind of geometry rather than a preference —
`Engine/Private/PhysicsEngine/PhysicsInterfaceUtils.cpp`:

    Chaos::EFilterFlags FlagsToSet = bTraceComplex
        ? Chaos::EFilterFlags::ComplexCollision
        : Chaos::EFilterFlags::SimpleCollision;

Simple or complex, never both. Both deposit meshes were imported with `"bCollision": false` and
`"bForceCollisionPrimitiveGeneration": false` — those are values in the import settings stored inside
the uasset, not names in its table — so they carry no simple primitives and a simple query cannot
see them however solid they look.

**The observation that discriminated** was already in the log and cost nothing: 1794 blocking hits in
one session, every single one of them the ship, none of them a deposit, while the player spawned 55 m
from `node_test_player_spawn_ferrite_a` and walked around it. The ship draws an engine basic shape,
which ships with simple collision. That rules out the sweep, the channel, the responses and the
character all at once, and leaves only the asset.

- **The fix was content**, in the Static Mesh editor: *Collision → Add Simplified Collision*, or a
  `UCX_` mesh alongside the model in the FBX, or a re-import with *Generate Missing Collision* on.
  A K-DOP wraps the whole mesh in one hull, so an asset holding several separate lumps wants *Auto
  Convex Collision* instead or the air between them becomes solid.
- `SpaceMMOSolidity::ReportIfIntangible` now warns at placement, naming both the deposit and the
  asset, because this is exactly the accepted cost ADR-0013 wrote down: a mesh without collision is
  silently intangible and looks precisely like a bug in the movement code. It measures the built
  mesh's primitive count rather than the import settings that produced it, since the two disagree the
  moment anyone edits collision by hand.
- **Rejected: sweeping with `bTraceComplex` true.** It would make every mesh solid by its render
  geometry with no authoring at all, which is the opposite of what ADR-0013 decided, and it puts a
  per-triangle query in the walk step of every character on the server.

### A character could be pinned against a ship and press forward for five seconds

**Measured rather than theorised, and the number is the whole diagnosis.** 638 consecutive blocked
frames covering 34 cm: about 6 cm/s against a walk speed of 600.

The resolve clamped the character to the contact point and discarded the rest of the step. In
continuous contact the sweep hits at once every frame, so the entire step was thrown away and the
only motion left was the separation push — 0.1 cm a frame, at 120 fps, times the 0.44 of the contact
normal that lay horizontal, is 5.3 cm/s. That is the figure the log shows. Nothing else produces it.

- `FCharacterWalkModel::SlideDeltaCentimetres` spends what is left of a step along the contact plane
  instead of nowhere, and the pawn sweeps up to three times so a corner resolves rather than pinning.
- **A sweep that begins inside something is handled apart from one that crosses a surface.** It
  reports the shortest way out, not a surface it met, and its impact normal describes nothing; worse,
  its `Hit.Location` is the sweep's own start, so resolving to it put the character back exactly
  where the frame began. The MTD normal is used to push clear and the step is retried.

### The diagnostic that could not see it, which is the fourth of these

`Blocked by %s at %s, normal %s, depth %.1f` printed every frame, and it was right every time. A
character leaning on a wall and a character who cannot walk print the identical line — an actor, a
normal, a depth of zero — and the only thing that separates them is how far the contact let the
character travel while it lasted. Finding that out took piping 1794 lines into a script.

Task 129 records three instruments that measured a thing against itself. This is a different failure
in the same family: an instrument that measured the event rather than the harm. Contacts are now
reported at their edges — one line when one begins, one when it ends carrying the distance covered
and the rate — and a contact lasting over a second at under 30 cm/s logs a warning that says it is
stuck rather than sliding, in words.

### Verified by playtest, 28 August

A character stops against a deposit and against a parked ship, slides along both rather than
sticking, and no longer pins under a hull. That is the only coverage this can have: every automated
run is `-nullrhi` and the pure half of it passes either way.

### The roof imported under a different name, and its collision matched nothing

Measured off the imported assets rather than taken from a successful import: eleven of the twelve
meshes hold `KConvexElem` data and `GB_Roof1` holds none.

**`GB_Roof` was both a material and a mesh group**, and Unreal imports meshes and materials into one
namespace. The material took the name, the mesh arrived second and was silently renamed `GB_Roof1`,
and `UCX_GB_Roof_01` then matched a mesh that no longer existed under that name. An intangible roof
looks exactly like a roof. It was the only name that collided, and nothing about the failure said so.

Materials are prefixed `MAT_` now, and `check_names_are_distinct` fails the run rather than trusting
anybody to remember — the rule is what matters, not the instance. Regenerated; the FBX carries
`GB_Roof` with its hull.

**Content chore, not urgent**: the roof is unreachable from inside and only matters to someone
landing on the building. Next re-import, delete the stale `GB_Roof` material asset and `GB_Roof1`
first, or the mesh will be renamed again and look exactly as it does now, then re-point the
Blueprint's roof component at `GB_Roof`.

### Station interiors

- The greybox generator emits collision — 87 `UCX_` hulls over 11 meshes, one per solid, since every
  solid in the building is an axis-aligned box and a box is already a convex hull. Re-run,
  re-imported, and playtested: walls stop a character and the floors hold one up.

  The two stair flights get **one ramp hull each rather than 25 tread hulls**, and that is a
  decision about how it plays rather than a saving. The character has no step-up, so a sweep into a
  17.3 cm riser slides sideways along it and an honest flight of stairs is a wall. One convex wedge
  through the tread nosings is a 26.6° incline, which the walk model already handles. It meets the
  ground slab flush at the foot and lands exactly on the Level 01 slab at the top — measured, not
  assumed: 0.000 m to 4.500 m over 9 m.

### Standing on geometry, which is what makes "inside" mean anything

`ResolveSurface` asked planets and nothing else, and it *snaps* the character to
`FPlanetTerrain::ResolveContact`. A floor slab is not terrain, so it held nobody up: walls stopped a
character the moment the hulls landed, and the Level 01 gallery could not be stood on at all.

`ResolveStanding` probes downward for geometry and stands on what it finds.

**Only ever a lift, never a drop**, and that one restriction is what makes "whichever support is
higher" fall out instead of having to be arbitrated. Terrain has already placed the character on its
own floor by the time this runs, so a surface found *below* the feet is one the height field is
already standing on top of, and is ignored. Walk into the building and the slab lifts you; walk into
it where the ground swells above the slab and the ground keeps you, with nothing deciding between
them.

**The rules for what counts as a floor are the ones ground contact already uses**, because a floor
that behaved differently from the ground would be two rules for one idea — a capture band widened
once already standing, so walking down a step does not go airborne at every one, and a separation
speed that always wins, so a jump leaves rather than being dragged back. `FCharacterWalkModel::
StandsOn` holds all three and is tested with no world.

The third rule is new, because a sphere has no walls: **a limit on how steep a surface may be**. A
downward probe run alongside a wall finds the wall, and standing on one would let a character walk
up the outside of the building. Fifty degrees, so the 26.6° stair ramp passes and the wall does not
— and the ramp angle is a case in the test rather than a number in a comment, so the stairs stop
being climbable loudly rather than quietly.

Two details that would each have been a bug:

- **The probe starts above the feet, not at them.** A character resting exactly on a floor otherwise
  begins its sweep inside it, and a sweep that begins inside something reports the way out rather
  than what is underfoot.
- **Standing is resolved along the character's own up, not along the surface normal.** Up on a
  planet is the direction away from its centre; that substitution is the whole walk model. Using the
  ramp's normal would tilt a body 26° while everything else in the building stood square.

**A character could jump onto a metre-high counter and sink into it.** Found by playtest, and the
mechanism is arithmetic rather than a guess.

Ground contact holds on out to *ten times* its tolerance once it has somebody — two metres, sized in
task 84's era for a ship crossing twelve metres of ground per frame — and when it holds on it
teleports the character onto the height field. A service counter is 1.1 m (`H_COUNTER`), so a
character standing on one was still inside that band: the ground claimed them and moved them down
1.1 m, which is to say into the middle of the counter. The floor probe then began inside the
geometry, reported the way out rather than what was underfoot, and correctly declined to answer.
Waist deep, on the floor, in a box.

The gallery worked throughout, and that is the discriminating half: 4.5 m is outside the band, so
the ground let go and the geometry answer stood unopposed. Everything under two metres failed and
everything over it worked.

**Nothing is applied now until both answers are in.** The planet loop records what the height field
decided instead of acting on it, the probe runs from where the character actually is, and the higher
of the two is applied once. Correcting afterwards was always going to work only while the ground
disagreed by a lot.

No test covers the arbitration, and one would be near-tautological: the comparison is a dot product.
What was wrong was the *order*, in a function that needs a world, so it is a playtest or nothing —
which is the same shape as the fault in task 129 and worth saying twice.

**The diagnostic was useless in the same way, again.** `Standing on …` was meant to log where
standing began and asked `bOnGround`, which `ResolveSurface` clears at the top of every call — so it
fired on nearly every call, twice a frame, **8622 times in one session**, and said nothing by saying
it constantly. It tracks what is underfoot now, and prints one line per floor.

**Unverified, and cannot be verified headlessly:** whether the dedicated server agrees. The client
predicts and reconciles softly toward the server, so if the server's world has no station in it the
server will think a character on the gallery is falling and drag them down. The deposit subsystem is
not gated by net mode, so a server world should place the same stations — but whether a dedicated
server signs in to the backend and therefore receives them has not been checked. It does not arise
in a standalone session, which is how this is played today.

## 131 — Ships fly through buildings

**Implemented, awaiting a playtest.** Belongs to **M7 — a world worth being in**, and finishes
[ADR-0013](adr/0013-terrain-is-a-function-everything-else-collides.md), which names "stations,
ships and deposits" and has so far been implemented for characters only.

Noticed in the playtest of task 130: a ship passes straight through the A-02 hull. **The ship is
solid to other things and asks nothing of the world itself.** `ASpaceMMOShipPawn::Hull` is
`QueryOnly` and blocks `ECC_Pawn`, so a walking character is stopped by a parked ship — but flight
sets position from `FShipFlightModel` and nothing sweeps, so nothing can stop the ship.

**Probably small, and deliberately not begun on the same playtest as 130.** A ship is already a
two-metre sphere to ground contact — `HullRadiusKilometres = 0.002` passed to
`FPlanetTerrain::ResolveContact` — so the shape exists and the seam is the same one the character
uses: sweep from the previous position to the wanted one, resolve the hit with the arithmetic that
is already pure and already tested.

**No design decision is needed to start**, and that is worth stating because it looks as though one
is. Ground contact cancels the motion into the surface and does nothing else — no damage, no
bounce — so a hull doing the same is consistent with what a planet already does, and hitting a
building at speed is a matter for the combat milestone rather than for this.

One thing to check rather than assume: the ship's sweep must not be blocked by its own docking or
by the character it carries, and it must not fight `ASpaceMMOShipPawn`'s own terrain contact the way
the character's floor probe fought the ground before task 130's second fix.

### What was built

`ResolveBlocking` sweeps between `Advance` and `ResolveGroundContact`, in the same three passes the
character uses: one to stop, one to spend the rest of the step along the surface so a glancing
approach scrapes rather than pins, one for the corner. A sweep beginning inside something is handled
apart, because it reports the way out rather than a surface it crossed.

**The sphere is the one ground contact already uses.** A ship has been two metres to the terrain
since it could land, and being a different size to a wall than to a hillside is the disagreement
nobody finds until they are wedged in a doorway. That also means the drawn hull and the collided
hull are separate numbers on purpose — see 132.

**The ground still resolves after, unchanged, and that is the risk worth naming.** The character's
floor probe fought terrain hysteresis until task 130 stopped applying either answer early; the ship
has the same shape of problem and has not hit it, because blocking acts horizontally against a wall
while terrain acts along the surface normal. If a ship ever wedges where a building meets the ground,
this is the first place to look.

### One arithmetic instead of three

The character, the ship and ground contact were each about to answer "how much of this motion
survives" in their own words. `SpaceMMO::Surfaces::SlideAlong` and `SeparationCentimetres` hold it
once; the walk model delegates and keeps its API and its tests.

What is deliberately *not* shared is the degenerate case, because the callers genuinely differ: a
velocity can be left alone and retried next frame, and a position is spent the moment it is applied
and must not be spent in a direction nobody measured. Both policies are pinned by tests, and the
mutation run shows they are — zeroing the ship's velocity resolve and handing back the unmeasured
delta each turn exactly one test red, and neither touches the character's.

`FPlanetTerrain::ResolveContact` keeps its own copy: it computes an impact speed in the same
expression, so the projection is not separable without splitting that too.

Also found on the way: `SpaceMMOFlightModelTests.cpp` closed its `WITH_DEV_AUTOMATION_TESTS` guard
two thirds of the way down, so 148 lines of reconciliation tests were compiling unconditionally
rather than only in automation builds. Moved to the end of the file.

---

## 132 — The starter ship is an engine cone

**Done 28 August.** The same move task 124 made for stations, one milestone later: the hull is named
in `DefaultGame.ini` rather than compiled in, so swapping the ship somebody flies is a line in a file
and not a rebuild. Points at `/Game/Ships/StarterShip`.

Scaled uniformly off the mesh's own bounds to `HullLengthMetres`, measured along whichever axis it is
longest on — a ship is longer than it is wide, so that is its length whatever orientation it was
authored in, which means the fit does not depend on the rotation being right first. The authored size
is logged beside the multiplier, because a model needing a large one is a model exported in the wrong
units and that is worth being told rather than silently living with.

**`HullRadiusKilometres` is deliberately not touched by any of this**, and the ini says so where
somebody changing the mesh will read it. That two-metre sphere is what ground contact has used since
a ship could land and what task 131's sweep uses now. A hull drawn twelve metres long that collides
like a two-metre ball is a disagreement nobody finds until they are wedged in a doorway, so both
numbers are set by hand and the log prints them together.

`SpaceMMOSolidity` moved from Backend to Core to be reachable from the ship, which is its third
caller. `StarterShip` carries convex collision, checked in the asset rather than assumed, so a
character can still walk into a parked ship.

## 133 — The ship cannot be seen from inside it

**Done 30 August.** Found in the task 131 playtest; belongs to **M5 — an
interface**, widened on 29 August to name perspective and controls (`design-bible.md` §8).

Board a ship and the third-person view shows no ship.

**The hull is drawn 77 metres from the ship**, and the camera boom hangs off the ship. Measured
rather than inferred: `SpaceMMO.Ship.HullIsDrawnWhereTheShipIs` loads the configured mesh and reads
its bounds off it —

    'StarterShip': extent V(X=358.22, Y=527.77, Z=163.40) cm,
                   bounds origin V(X=-1687.33, Y=-7499.78, Z=-0.00) cm

That origin is the FBX object's own scene position, (-16.873, 74.998, 0) metres, baked into the
vertices at import. Scaled by the 1.137 fit it puts the hull 87 m from the pawn — out of frame from
inside, and from outside a parked ship sitting 87 m from its own boarding prompt, which was easy to
miss while boarding reached 90 m.

**Ruled out already, without a playtest.** The ship defaults to third person —
`FirstPersonCamera->SetActive(false)` in the constructor — so it is not a camera-toggle state
carried over from the character. The boom is 1200 cm and the hull is fitted to 12 m, so the camera
is not inside the hull. And the mesh's own pivot is centred: importing `StarterShip.fbx` into
Blender and reading the vertices gives x -3.6..3.6, y -5.3..5.3, z -1.6..1.6, centre (0, 0, 0), so
the hull is not authored off to one side of its origin.

**The leading candidate, and the one measurement that settles it.** In the FBX the *object* sits at
(-16.9, 75.0, 0) in its scene — about 77 m from the scene origin. If the import baked that transform
into the vertices, the hull is drawn ~87 m from the pawn once scaled, the camera boom hangs off the
pawn, and the ship is simply not in frame. From outside, a parked ship would sit 87 m from its own
boarding prompt, which is easy not to notice: the last boarding in the log happened from 21 m away.

### Fixed by a re-import, not by code

**Re-imported with *Bake Meshes* off**, and the test that found it read
`extent V(X=3.58, Y=5.28, Z=1.63) cm, bounds origin V(0) cm`.

**That setting was reverted the same evening** — see task 139. Turning it off moved the render mesh
and not its collision, which traded a ship you could not see for one you could walk through. The
pivot is corrected at load instead, so the importer is back at its defaults. The suite was red on that one test in
between, deliberately: it is a real defect in shipped content and the test's job is to say so rather
than to be softened into a warning.

*Bake Meshes* is Interchange's name for it. *Transform Vertex to Absolute* is the legacy FBX
importer's name for the same setting and does not appear in UE 5.8's dialog, which is worth knowing
before somebody goes looking for it.

**The re-import also dropped the object's 100x scale**, which had been baked in alongside the
translation: the mesh is 10.56 cm on its longest axis now rather than 1055 cm. Nothing is wrong with
how it draws, because `HullLengthMetres` fits it and the fit exists for exactly this — but it is
scaled 113.6 at runtime, and the asset now misstates its own size to anything that reasons from
bounds, which is LOD screen sizes and lightmap resolution. Setting *Build Scale* to 100 in the mesh's
build settings, or *Import Uniform Scale* to 100 on a future re-import, would put that right.

**The proportions are unchanged** — 3.58 : 5.28 : 1.63 against the old 358 : 528 : 163 — so the axes
did not move and the yaw of -90 derived from them still holds.

Two alternatives were considered and rejected.

- **Rewriting the FBX** so its object sits at its scene origin. Tried, and reverted: it is a bought
  mesh, a Blender round-trip rewrites its normals and smoothing, and there is no way to check the
  shading of a ship headlessly. The raw file is byte-identical to the vendor's again.
- **An equal and opposite `HullMeshOffset`.** It would work today and become wrong the moment
  anybody re-imports the asset properly — a compensating lie in a config file, waiting.

The hazard with a checkbox is that missing it looks exactly like doing it, which is the shape of
fault this project has paid for repeatedly. It is covered here: the test fails until the bounds
origin is inside the hull, so the re-import either worked or the suite says it did not.

**Also measured: the hull is authored nose-along-Y, and +X is forward.** Its extent is 358 cm across
X and 528 across Y, and bucketing the vertices along Y puts the narrow end — the nose — at Blender
-Y, which UE's axis conversion lands on +Y. `HullMeshRotation` is a yaw of -90 now, to turn +Y onto
+X. If it flies tail first it is +90, which is one value in the ini and no rebuild. `ApplyHullMesh`
logs the bounds origin, the extent and which axis is longest, so none of this has to be re-derived.

---

## 134 — Stepping out of a ship puts you thirty metres away, facing nowhere

**Done 30 August**, awaiting a playtest. Belongs to **M5**.

`FBoarding::DefaultStepOutOffsetKilometres` is **0.03 km**, so a character steps out thirty metres
to the ship's right. That was chosen when a ship was an engine cone of no particular size and
nothing was near it; beside a 12 m hull it reads as being teleported into a field.

Wanted: **at the side of the hull, facing the way the ship faces.** The offset should come from the
hull rather than being a constant — half its width plus a step — which also stops it going wrong
again the next time the ship changes size. Nothing sets the character's rotation on step-out at all
today, so it faces wherever the freshly spawned pawn happens to.

Worth doing at the same time, because it is the same constant's twin: `DefaultBoardingRangeKilometres`
was **0.1 km**, and the log shows a boarding from 90 m away. Reaching a ship from the length of a
football pitch is the same fault at the other end of the trip.

### What was done

The offset is measured off the hull — the largest horizontal half-extent plus a metre of clearance —
so a bigger ship steps you out further and this cannot go stale the next time the drawn ship changes
size. The largest half-extent rather than the width specifically, because which of a mesh's axes is
its width depends on how somebody authored it, and landing inside the hull is worse than landing a
metre further out than was needed.

Taken from the mesh's extent rather than from the component's world bounds, deliberately: task 133's
hull has bounds 77 m from the pawn, and this must not inherit that fault.

`FBoarding::StepOutRotation` faces the character along the ship's nose, flattened into the tangent
plane. Nothing set a rotation at all before. The degenerate case is why it is a function rather than
two lines at the call site — a ship parked nose-straight-up leaves nothing to flatten, and the naive
version produces a NaN, which does not face the wrong way but makes the character vanish.

Boarding range is 0.015 km. `SpaceMMO.Boarding.Range` asserted 0.09 and 0.11 against a range of 0.1
and so failed the day the range was tuned — not because anything was wrong, but because it asserted
the number instead of the rule. It derives both from the constant now, and adds the rule that was
actually wanted: a ship is boarded from beside it, not from across a field.

---

## 135 — A ship may move when you step out of it

**Done 30 August**, confirmed by playtest: no drift after the stick is centred. Belongs to **M5**.

Which of the three candidates below it actually was stays unproven — stepping out thirty metres away
was fixed in the same build, so "the ship moved" and "I was looking at it from somewhere unexpected"
were both removed at once. The step-out line logs both positions now, so if it ever returns the log
answers it rather than another round of reasoning.

Reported in the task 131 playtest: the ship appears to shift when the character disembarks.
`ServerDisembark` deliberately does not touch the ship — "the ship stays exactly where it is,
unpossessed, waiting to be climbed back into" — so if it moves, something else moves it.

Candidates, none of them checked:

- **The ship keeps simulating after it is unpossessed.** `Tick` still runs `SimulateStep` on the
  authority, so `PendingInput` frozen at whatever was last held would keep thrusting.
- **Task 131's blocking sweep**, which is new and runs every frame the ship moves at all.
- **It may not be moving.** Thirty metres of step-out (134) puts the camera somewhere unexpected,
  and a ship seen from a new angle looks displaced. Fix 134 first and look again — it may take this
  with it.

**Do not guess between these.** `PendingInput` and the ship's position across the disembark are two
numbers and one log line, and this is the third time in this file that reasoning stood in for one.

### What was done

`UnPossessed` is overridden and centres the stick. **A ship nobody is flying must not keep flying**,
and nothing cleared `PendingInput`, so a ship left with thrust held kept thrusting after its pilot
stepped out. That is a fault on its own terms whether or not it is this one, and every route out of
a ship comes through that override — including ones nobody has written yet. It logs when it had
something to clear, so "the stick was held" is a fact next time rather than a candidate.

Stepping out logs the ship's position alongside the character's now, because "the ship moved when I
got out" and "the ship is where it was and I am looking at it from somewhere new" produce the same
impression and different numbers. With 134 fixed the second is the more likely of the two, and the
log line settles it either way without another round of reasoning.

---

## 136 — Sprint

**Done 31 August**, awaiting a playtest. Belongs to **M5** for the key, and to **M6** for the pool
behind it.

Hold Shift to run. The design bible already names it: §2 gives `stamina` as "Sprint, jump, exertion
pool", and defers the skill to the combat milestone because a pool needs an XP source first.

**Jump is the precedent.** It is on the same skill and it works today without any stamina existing,
so sprint is a multiplier on `FWalkConfig::WalkSpeed` now and gains a pool later without the movement
code changing shape. `bSprint` travels in `FWalkInput` alongside `bJump`, because the server
integrates this model — a client that simply moved faster would be a client disagreeing with the
server about where it is.

**A ceiling, not a shove.** Sprint raises the speed the model accelerates toward and changes nothing
else. Adding a push would make tapping the key a lunge; multiplying the acceleration would make a
sprinting character turn sharper than a walking one, which reads as the controls changing under you.
The test pins both ends: the settled speed differs, and the first step off the mark does not.

It applies in the air too. A character who loses their run the instant they leave the ground
decelerates mid-jump, and a jump that travels less far the faster you were going feels broken without
being explicable. Air control is already a quarter of ground acceleration, so it changes little.

Shift is free on foot — `ShipBoost` already has it, and that is a different pawn with different
bindings. The animation needs nothing: the blend space is one-dimensional on ground speed, so a
faster character plays its fastest clip.

---

## 137 — The mouse wheel zooms the third-person camera

**Done 31 August**, awaiting a playtest. Belongs to **M5**. Both on foot and in the ship.

The booms are already there — `CameraBoom->TargetArmLength`, 400 cm on the character and 1200 on the
ship — so this is a bound axis, a clamp and a rate.

**Client-only, and that is a rule rather than an implementation note.** `design-bible.md` §8: "the
camera is a client concern only — it must never affect server-side validation, which is why
interaction range is checked against the pawn, never the camera." Zoom does not travel in
`FWalkInput`, is not replicated, and is applied outside the simulated step in both pawns.

**Proportional, not a fixed step.** A notch is worth a fraction of the current distance, so it feels
the same close in and far out — a fixed number of centimetres is either unusably coarse near or
unusably slow far, because what anybody perceives is how much the view changed by. 150 to 900 cm on
foot, 600 to 3000 in the ship, eased over 0.15 s so a fast scroll glides.

**Remembered off the pawn.** A character does not survive being boarded — stepping into a ship
destroys it and stepping out spawns another — so a zoom kept on the pawn would reset on every trip.
`USpaceMMOViewSubsystem` holds both distances on the game instance, which is per client and outlives
everything.

### The test that passed for the wrong reason

`ZoomStepsByProportion` was green against a mutation that replaced the whole thing with "subtract 60
centimetres". Fifteen per cent of the four hundred it was started from *is* sixty, and a fixed step
reverses exactly as cleanly as a proportional one — so every assertion in it held, including the one
about going in and back out landing where it started.

It asserts the property that actually separates them now: a notch from twice as far out moves twice
as far. **The mutation run is the only reason anybody knew**, which is the whole argument for doing
one rather than reading the test and nodding at it.

---

## 138 — Holding Alt orbits the camera without turning the pawn

**Done 31 August**, awaiting a playtest. Belongs to **M5**. Both on foot and in the ship.

Hold Alt and the mouse swings the view around the character or ship; release it and the view returns
to sitting behind them. The pawn does not turn while Alt is held.

**The awkward part is not the orbit, it is what the pawn is doing meanwhile.** On foot, mouse X
turns the character through `FWalkInput::Turn`, which the server simulates; the orbit has to
suppress that without suppressing movement, so a character can keep walking forward while the camera
swings around to look at them. Same in the ship for yaw.

**And the same §8 rule applies**: the orbit is a client concern and must not reach the simulation. A
camera that turned the pawn would be a camera that changed what the server sees. So the input is
stopped before it reaches `PendingInput` rather than undone afterwards — a turn sent and then
cancelled is a turn the server performed.

**Returning is where the arithmetic is.** `FViewOrbit::Recentred` eases the swing back by the short
way round: a camera at 190 degrees comes home through 180, not the other 170, or looking behind
yourself spins the view most of a full turn on release. Exponential rather than linear, so it arrives
without a stop.

**The first version did not arrive.** Three time constants left five per cent of the swing
outstanding when its time was up — four and a half degrees off a ninety degree orbit, a camera that
visibly settles late and then creeps. The test asked whether it was home when it said it would be and
it was not; it is five time constants and a degree of slack now, with a second assertion that it is
*still on its way* at the halfway mark, so "eases" cannot quietly become "snaps".

**The state is shared with the ship** rather than written twice. From the camera's side a hull and a
body are one problem and only the suppressed input differs, and two copies would be two places for
the feel to drift apart.

## 139 — A character walks through the ship, which has collision a hundred times too big

**Done 31 August**, verified by playtest and by measurement. Belongs to **M7 — a world worth being in**, with
[ADR-0013](adr/0013-terrain-is-a-function-everything-else-collides.md); found in the task 133
playtest, and caused by the re-import that closed it.

Measured, headlessly, by `SpaceMMO.Ship.HullCanBeBumpedInto`:

    'StarterShip': 1 simple primitive(s), collision trace flag 0.
    collision extent V(X=358.32, Y=527.87, Z=163.50) cm
         against render extent V(X=3.58, Y=5.28, Z=1.63) cm.

**Exactly a hundred to one.** Task 133's re-import dropped the object's 100x scale along with the
baked translation; the render geometry shrank and the convex hull did not. Multiplied by the runtime
fit of 113.7 the collision is some four hundred metres across, around a ship drawn twelve metres
long.

**Build Scale was the wrong advice, and it was given confidently.** The engine's mesh builder
applies `BuildScale3D` through `ScaleStaticMeshVertex` to vertex positions and never mentions
`AggGeom`, which reads as "scales the render mesh, leaves collision alone". Setting it to 100 did the
opposite: collision went 358 to 35832 and the render extent did not move. The ratio went from a
hundred to one to ten thousand to one.

Reading the engine is still the right instinct — it is what killed two console-variable theories in
one sitting once — but a function that is *not* mentioned in a file is much weaker evidence than a
value read off the built artefact, and this was treated as though the two were the same. **Set Build
Scale back to 1.**

### The code stopped caring instead

`ApplyHullMesh` measures the mesh's bounds origin and subtracts it, rotated into the parent frame.
Three import round trips went into trying to make a pivot be zero; measuring it and cancelling it is
one line and works for every hull anyone exports in future.

That retires the class. **Where** the geometry sits relative to its pivot no longer matters, and
**how big** the mesh claims to be never did, because `HullLengthMetres` fits it off its own bounds.
`SpaceMMO.Ship.HullIsDrawnWhereTheShipIs` was deleted with it: the thing it insisted on is handled
now, and a test that demands a particular import setting is a test that sends somebody back to a
dialog.

What is left is the one property of an asset that code cannot compensate for — **the mesh and its
collision disagreeing with each other**. `HullCanBeBumpedInto` asserts that ratio and says nothing
about the size, which is allowed to be anything.

### The complete picture, once collision was measured for position and not only size

    collision  extent (358.32, 527.87, 163.50) cm  at (-1687.33, -7499.78, 0)
    render     extent (  3.58,   5.28,   1.63) cm  at (0, 0, 0)

**The import applied the FBX object's transform to the collision and not to the mesh.** With *Bake
Meshes* off the render geometry got neither the 100x scale nor the 77 m translation, and the
collision got both. Every measurement taken before this one was of extents, which cannot see half of
it.

**That also explains the part that was filed as unexplained.** The sweep reported nothing not because
a sweep beginning inside a hull is mishandled, but because the character was never inside it: 7499.78
cm of offset multiplied by the runtime fit of 113.7 puts the collision **8.5 km** from the ship. It
was not near anybody. No second theory was needed, and the note that said "measure again rather than
layering a second theory on the first" was right.

**And it explains why the automatic centring did not save it.** The pawn subtracts the *render*
bounds origin, which is (0, 0, 0) here, so it correctly did nothing — while the collision sat
somewhere else entirely. Centring can move the two together; it cannot fix them disagreeing, which is
the one thing the test is for.

**Re-imported with *Bake Meshes* on** — the original default. Collision is unaffected by that
setting, so it stayed at 358 @ (-1687, -7500) and the render mesh joined it there instead of sitting
at the origin:

    collision  extent (358.32, 527.87, 163.50) cm  at (-1687.33, -7499.78, 0)
    render     extent (358.22, 527.77, 163.40) cm  at (-1687.33, -7499.78, 0)

Same size to within a millimetre, and the same place. The pawn's centring subtracts that shared
origin and puts both on the ship. A character walks into a parked ship again.

**The 77 m pivot is still in the asset and no longer matters**, which is the point of having moved
the correction into code: the import setting that was fought over for four rounds is back at its
default, and the thing it was being fought about is measured and cancelled at load.

### What was ruled out first, and what was unexplained until the position was measured

The character's sweep is fine: the same session logged 23 `Blocked by BP_Station_A02_CapitalHub_C_0`
and not one against the ship, which clears the channel, the responses, the capsule and the walk code
in one reading. And the ship stopping at the station says nothing about its hull — that is the ship's
own two-metre sphere from task 131, not the mesh.

`CTF_UseComplexAsSimple` was the other candidate and the flag says 0, so the hulls are reachable in
principle.

**Why the sweep reported nothing at all** was carried as unexplained for two rounds, with a note not
to theorise about it until the numbers underneath it were sound. That was the right call: the answer
fell out of the position measurement above and needed no theory.

### The check that should have caught it

`SpaceMMOSolidity::ReportIfIntangible` counted collision primitives and stopped, so it passed a mesh
whose collision was a hundred times the wrong size. That is its own version of the mistake the
testing rules warn about: asserting that a value exists rather than asserting the value.

It now also warns when the collision has drifted from the mesh by more than a factor of two, and when
the collision complexity is *Use Complex Collision As Simple* — which leaves hulls present, drawn in
the editor, counted correctly, and unreachable to every sweep this project runs.

**And the test measures where the collision is, not only how big it is.** Four measurements of this
asset were taken before anybody asked that question, and the answer was the whole fault. Size was
only ever half of "they agree".

## 140 — A failing UDP transport failed a different innocent test every run

**Done 31 August.** Environment rather than code.

Two consecutive suite runs failed two entirely unrelated tests —
`SpaceMMO.Patch.AssemblesLikeTheGlobe`, then `SpaceMMO.Planet.SurfaceGravityMatchesConfig` — and
neither had been touched in weeks. The cause was in the log just above them:

    LogUdpMessaging: Error: Sender FUdpMessageProcessor.Sender:
    SendTo failed (destination: 230.0.0.1:6666) (SE_EINVAL)

**An Error-level log during a test fails that test, whatever wrote it.** The automation framework's
own multicast transport started failing on this machine — ten occurrences in one run, and *zero* in
every archived log before that day — so the error landed on whichever test happened to be in flight.

**A red suite that names a different innocent test each run is worse than a red suite**, because the
first thing anybody does is go and read that test. Two runs went that way before the pattern showed,
and the only reason it showed at all is that the second failure was somewhere else entirely.

The transport is off in `DefaultEngine.ini`. The message bus the automation runner uses works
in-process; UDP is one transport for it and nothing here needs one. Off rather than silenced, because
suppressing the log would leave it failing quietly instead.

**`EnableTransport` is the switch and `EnabledByDefault` is not.** The first attempt set the latter,
one run passed, and the next failed — which is exactly what a setting that did not take looks like
from outside, and the same trap as a diagnostic that can silently not run. Reading the property list
in `UdpMessagingSettings.h` settled it in a minute.

## 141 — There is no crosshair

**Done 31 August**, awaiting a playtest and one Widget Blueprint. Belongs to **M5 — an interface**.

Nothing marked the middle of the screen, on foot or in a ship. Sketched before it was built, per the
rule that interface gets a yes first, and all three defaults were taken.

**Screen centre rather than the pawn's forward.** It is what people expect, and when combat arrives
in M6 aiming will almost certainly be camera-relative. Projecting the character's own forward is more
honest about the pawn but wanders off-centre whenever the camera lags, which reads as a bug rather
than as precision.

**Faded while Alt is held.** Mid-orbit the camera points somewhere the pawn has no opinion about, so
a reticle there is actively lying. Hidden outright whenever a screen is open, which is a different
question and answered in the controller: one is about whether the view still means anything, the
other about whether the player is looking at the world at all.

**And a velocity marker in the ship**, which is the half that earns its place. This flight model has
real inertia, so pointing one way and travelling another is ordinary, and nothing on screen said so.
The reticle says where the nose is; the ring says where the ship will end up. When they coincide it
is flying straight.

### Where the arithmetic is, and the two ways it goes wrong

`FCrosshairMarker::ScreenOffset` is handed a direction already in the camera's frame, so both awkward
cases can be pinned down without a viewport.

- **Screen Y runs down and a camera's up runs up.** Getting that negation backwards looks perfectly
  correct in every still frame — centred flying straight, right when drifting right — and is wrong
  only while climbing. The test asserts the sign.
- **A direction behind the camera projects to the mirror of where it belongs.** A ship here routinely
  travels backwards; turning to face a station while still carrying the velocity that got you there
  is the ordinary way to arrive. Projected, the marker would sit on the wrong side and a pilot
  following it would turn away from where they were going. It is pinned to the side the direction
  actually lies on instead, and a direction straight out of the back is not drawn at all — there is
  no side to choose, and choosing one sends somebody turning in a direction nothing picked.

Both were verified by mutation: dropping the negation and pinning to the far side each turn exactly
one test red.

### Drawn, not assembled

Every other element of this HUD is a Widget Blueprint of rows and text. This one is four ticks and a
ring whose position is recomputed every frame, so it is drawn in `NativePaint` — laying it out in
UMG would put half the reasoning in an asset nobody can diff and the other half in code reaching into
it by name.

The Widget Blueprint still exists and is still named in `DefaultGame.ini`, for the same reason as the
others: it is how the thing gets a class to instantiate and a place to hold its style. **It is empty
of widgets**, and creating it is the one editor step — a Widget Blueprint parented to
`SpaceMMOCrosshair`, saved as `/Game/UI/WBP_Crosshair`.

Two passes on every line, a thicker dark one under a lighter one, because no single colour is legible
everywhere: white vanishes against the ore deposits and against sunlit terrain, black vanishes against
space.

**It says nothing the simulation reads**, which is `design-bible.md` §8 and worth stating because a
crosshair is the most tempting thing on a HUD to quietly promote into an aiming rule.

### And then the character was standing in it

The first playtest put the reticle over the character's head, which is what a centred camera and a
centred crosshair do.

**The camera steps aside, and the pawn never does.** Sliding a character sideways to make room would
put its real position somewhere other than where it is drawn -- the same fault as a hull 77 m from
its pivot, differing only in scale. `USpringArmComponent::SocketOffset` carries it, and that one
rather than `TargetOffset` because it is applied in the arm's rotated frame: the shoulder stays over
the shoulder through a pitch or an orbit, where a world-space offset would slide across the character
as the view came round.

**Over the right shoulder on foot; straight up in the ship.** A hull framed over one shoulder reads
as flying from beside your own ship, so the camera lifts instead and the hull sits low with clear sky
ahead of the nose — which is also where the velocity marker needs room to roam.

**Scaled with the zoom**, or the framing holds at exactly one distance: an offset that composes well
at four metres is proportionally three times as wide wound in to one and a half, and puts the
character half off screen. Verified by mutation — replacing the scaling with the authored offset
turns the framing test red.

**The ship's lift was picked by eye and was half of what it needed.** 90 cm against a hull 186 cm
tall left the reticle squarely on the engine glow; the shift was real, about 72 px, and swallowed by
a hull some 300 px tall.

The arithmetic says it should never have been a distance at all. **Whether the reticle clears the
hull does not depend on how far back the camera is**: the hull's on-screen half-height and the
camera's lift both fall off with distance in the same proportion, so it is a ratio of two centimetre
figures and nothing else. Anything above one clears it; the rest is how much sky sits between.

So it is a multiple of the hull's own drawn half-height now, measured when the mesh is applied, and
logged — which also means it stays right if the ship is ever swapped, where a hand-tuned figure
would quietly stop being. The character keeps a written offset, because there the goal is a
composition rather than a clearance and no measurement of the body tells you where a shoulder
belongs.

## 142 — EconSim hand-copies the recipe numbers it simulates

**Done 31 August.** Belongs to **M1 — backend economy core**, which is where EconSim was built, and matters
to **M4** because that is where the recipes get tuned.

Found on 31 August while answering "is the ore-to-plate ratio load-bearing for EconSim". It is, and
not in the way expected: the sim does not read the ratio, it **repeats** it.

`Bots.cs:66` runs refining as `Sim.Ore, 20, Sim.Plate, 4`, and `data/recipes/core.json` says
`refine_ferrite_plate` takes 20 ore and yields 4 plates. They agree today. Nothing makes them agree
tomorrow.

The same shape appears at least three more times:

| EconSim | Authored content |
|---|---|
| `Bots.cs:66` refining 20 → 4 | `refine_ferrite_plate` |
| `Bots.cs:72` shipcrafting 4 → 1 | `build_shuttle_hull_section` |
| `Sim.OrePerFrame = 10` | `build_alloy_frame`, ten of each |
| `SimWorld.SeedPrice` | the item pack's own prices |

**The comments already know.** `Sim.OrePerFrame` says "Ten of each, from `build_alloy_frame` in
`data/recipes/core.json`", and `Sim` itself says "Keys match `data/items/core.json`". Somebody wrote
down where the numbers came from, which is exactly right, and nothing checks they still match.

**Why it matters more than an ordinary duplication.** EconSim's output is used to justify decisions
— ADR-0008 rests on frames staying worth building, and the freighter exists because a five-year run
found composite frames piling up unsold. A sim that quietly models the old recipe answers the
question anyway, confidently, about a game that no longer exists. That is the same failure as a
diagnostic that reports on an experiment which never ran, and it costs more here because nobody is
watching for it: the sim always produces plausible numbers.

### The better fix is ruled out, and by an ADR rather than by taste

Having EconSim read the pack would remove the duplication instead of policing it, and that was the
preferred option until the constraint turned up. `ContentLoader.ReadAsync` is static and touches no
database — but it lives in `SpaceMMO.Data`, and EconSim's project file says outright: *"References
SpaceMMO.Domain only — no database, no HTTP. If this ever needs SpaceMMO.Data, something has leaked
out of Domain that belonged in it."*

Moving the reader into Domain is the obvious response and
**[ADR-0003](adr/0003-topology.md) forbids it** — its module-boundary decision is that "`Domain` has
no I/O", and the project file spells that out as "no database, no HTTP, **no file I/O**, no clock, no
randomness". So the choices were a second JSON reader living inside the tool — trading a
duplication of numbers for a duplication of the reader that parses them — or leaving the numbers and
guarding them.

The numbers are what change; the file format almost never does. And a drifting reader fails loudly
where drifting numbers fail silently, which is the whole complaint here.

### What was built

The literals are named constants on `Sim` now, each citing its recipe key, and
`EconSimMatchesTheContentPackTests` reads the real `data/` directory and checks all four against it:
refining, shipcrafting, the frame's ten-of-each, and that every item the sim trades is actually
shipped. `SpaceMMO.Data.Tests` gained a project reference to the tool to do it, which is unusual and
explained in the project file.

**Verified by tuning the content rather than the code**, which is the drift this exists to catch:
changing `refine_ferrite_plate` from four plates to three turns exactly one test red, and the message
names the recipe.

The frame test carries the reason it matters in its own body — ADR-0008 rests on a frame being the
point where material crosses the faction line, so a frame that stops needing four ores stops being
the reason anybody trades across it, and the contested zone becomes scenery.

**Seed prices are deliberately not covered.** `SimWorld.SeedPrice` opens markets that have never
traded, and the pack's `factionBuyPrice` is a standing order on raw materials. They are different
numbers answering different questions, and asserting one against the other would invent a
relationship nobody decided on.

## 143 — The greybox method is a skill, not one build script

**Done 31 August.** Belongs to **M7 — a world worth being in**. Nothing in the game references any
of it; this is tooling and one more unbuilt shell.

A-02 cost a playtest, a flicker report and an impassable corridor to get right (127, 144), and every
one of those faults was invisible in the drawing and invisible in the source numbers. All of that
knowledge lived in one build script for one building, which is the same failure this file exists to
prevent, one level up.

`.claude/skills/blender-greybox/` now carries it. `SKILL.md` has the workflow and the reasoning;
`references/` has what is only worth reading when relevant — how each check works and what it
becomes for a ship or a character, headless Blender, and Unreal's naming and collision rules.

**`scripts/greybox_lib.py` is the payload, not the prose.** Geometry primitives, all four checks,
collision hulls, the render setup and the FBX export. A future run spends its effort on the
transcription instead of rediscovering that an area light in frame blows out the shot, or that
Workbench renders every ceiling black from below, or that a stair tread level with its landing
fights the floor it lands on.

**The four checks are the method**: derive the scale from the source and prove it against a second
dimension; assert the model against the numbers the source publishes; drive cross-material
coincident faces to zero; measure every gap against the character that walks through it.

**Tested by building A-01 Trading Hub from the skill alone**, a sheet nobody had greyboxed, and it
earned itself on the first run. The derived scale proved itself on three independent numbers — the
6 m workshop dimension, the airlock at exactly the parts schedule's 6.0 x 3.5 m, and both room
areas — and the schedule check then confirmed 454, 150, 21 and 625 m² against the sheet. It went on
to find **two impassable gaps** (0.15 m beside the quest stand and 0.45 m behind it, both from
putting furniture exactly where the drawing's rectangles sit), three 0.95 m slots behind the
industry bays, and ten coincident faces over two rounds — the last four being 0.04 m² slivers where
the roof seam landed on a wall junction.

**`greybox_lib.py` is committed twice on purpose.** Once in the skill, so the skill is portable and
self-contained; once in `tools/greybox/`, so a build script imports it from beside itself rather
than depending on where skills happen to live. If they drift, the skill's copy is the original. The
alternative — build scripts importing out of `.claude/skills/` — couples content to a tooling path,
which is worse than a duplicate file that is regenerated rather than edited.

**What it has not been tested on is anything but an architectural plan.** A-01 is the closest
possible case to the one the skill was written from: same artifact, same conventions, same
draughtsman. The ship and character guidance in it is reasoned rather than exercised and should be
read as a hypothesis until somebody builds one. That is worth knowing before trusting it on a hull.

**A-01 is a greybox and nothing points at it.** It sits in `client/RawContent/Stations/` beside
A-02, wired to no station key and no Blueprint, exactly as A-02 is. The plan set's build order puts
collision on station meshes and R4's decision ahead of either being walked into for real.

## 144 — A plan drawn to centrelines does not say whether a person fits

**Done 31 August**, from a playtest: Joe walked the A-02 greybox and could not get through parts of
it, worst on Level 01. Belongs to **M7 — a world worth being in**, and it is a rule for every sheet
in the plan set, not a fix to one of them.

`ASpaceMMOCharacterPawn` sweeps a capsule of `CollisionRadiusCentimetres = 34` and stands
`CharacterHeightCentimetres = 180`, so **a character is 0.68 m across**. Every dimension on the
Origin Station Plans is to wall centrelines, which is not the space anybody walks in: two walls a
metre apart on the drawing leave 0.50 m of air once each takes its 0.25 m, and 0.50 m stops the
pawn dead. **Nothing about the drawing looks wrong when a corridor is impassable.**

**Measured, not eyeballed.** `report_tight_gaps` takes every pair of solids that obstruct walking,
finds those facing each other across a gap within the band a standing character occupies, and
reports the clear width. Eleven failures, and the split matters:

- **One impassable route.** The Level 01 south gallery walk, 0.50 m clear over 4.1 m — and it is the
  *only* way from the landing to the six career alcoves. The sheet draws the core wall at `y=31.8`,
  0.8 m off the void edge; wall and parapet eat 0.75 m of that. Moved to 32.6 for 1.30 m clear,
  which is the most the drawn stair at `y=33` allows without moving the stair too.
- **Ten dead slots**, all 0.25 m: eight market terminals and the hangar racking standing a quarter
  metre off the wall behind them, because that is where the plan's rectangles sit. Not routes, but
  a gap a pawn cannot enter and cannot see the back of. Seated flush.

**The threshold is 1.20 m, not 0.68 m.** The pawn plus a hand either side. Under 0.68 is a wall with
a gap in it; between the two is somewhere a player scrapes along and the camera fights the geometry.
Furniture goes flush to its wall or 1.2 m off it, never a quarter metre.

**Recorded where the next sheet will be read, not only here.** Constraint 3 on the plan set now
states the pawn's width, shows why a centreline dimension hides it, and gives the 1.2 m rule; A-02's
notes carry the specific change. The set has seven sheets and six are unbuilt, so the cheap moment
to write this down was before any of them are.

**No siblings to fix yet, and that is worth saying plainly.** A-01 and A-03 to A-07 have the same
trap in them, because they are drawn the same way — but none is greyboxed, so there is nothing built
to measure. The check runs on whatever is built next, which is where it will catch them.

**Numbered 142 until 31 August.** Two sessions took the next free number in the same hour.
The other 142 was committed first and is cited from the milestone list above, so this one
moved rather than that one. Nothing outside this file referred to the old number.

## 143 — A character is dropped into the world from fifty metres up

**Done 1 September**, awaiting a playtest. Belongs to **M5 — an interface**, as part of what the
opening feels like.

Spawning fell. `SpaceMMOGameMode` places a character above where the ground will be and lets contact
catch them, and the comment says why: *"so ground contact catches the character rather than the spawn
positioning it onto the ground by hand — which is what proves the height function and the mesh agree
about where the ground is."*

**The deeper reason is not that, and it is why this could not simply be deleted.** A connection is
given its pawn 323 ms before the world has a planet in it — measured, and recorded in the game mode.
At the moment of spawning there is nothing to ask where the surface is. Dropping is robust to that,
and robust to the planet arriving wearing terrain the spawn had never heard of, which is task 129's
whole subject.

So the character is placed **the first time a planet appears** rather than at spawn: same height
function contact uses, applied at once instead of after a fall, and answered against whatever terrain
the actor is wearing by then. Not the spawn positioning somebody by hand — the ground being met
immediately.

It logs `Stood on the ground at ... rather than falling to it.`, so a spawn that still falls is a
spawn where no planet was ever found, which is a different fault with a different fix.

---

## Done

Nothing yet under this file's numbering.
