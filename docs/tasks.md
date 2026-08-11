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

---

## 84 — Find why the terrain patch does not draw

**In progress.** The 2026-08-10 playtest moved this on, and narrowed it considerably.

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

`bRecipeChanged` now includes `bWantsGlobeComponent`, so the switch works. It has not been re-run
since it started working. That is the next thing to do, before any more of the mesh is investigated.

Predicted outcomes, recorded first: ground appearing means the patch's *mesh* is fine and the
`GroundPatch` *component* is at fault — its absolute location and rotation, its attachment to the
globe, or its visibility — which is a different and far more tractable bug. Ground staying black
means the mesh really is at fault, established for the first time with a switch that actually takes
effect.

The detailed ground patch has never appeared on screen. The globe carries the ground instead, at a
vertex every 331 m, which is the resolution the player is currently walking on.

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

**The obvious next experiment is the missing cell of a 2x2.** Three combinations have been tested:
globe mesh in the globe's component draws; patch mesh in the globe's component is black; patch mesh
in its own component is black. **Globe mesh in the patch's component has never been tried.** If it
draws, the component is exonerated and the fault is in the mesh, as currently believed. If it is
black, the component matters after all and `PatchIntoGlobe`'s black result had some other cause —
which would reopen the question the whole investigation turned on.

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

**Blocked on 84.** Reconstructed, not recovered: this is the task most likely to be the one
described as "tied into" the terrain work, but its original content is gone.

`SpaceMMO.HideGlobeUnderPatch` defaults to 0, so the globe draws the ground and the patch refines
nothing. The two are samplings of one height function, so drawing both puts the globe's coarse hills
through the patch's fine ones — only one may ever be visible.

Once the patch draws, default the flag to 1 and confirm the handover in both directions: descending
into the atmosphere (globe hidden) and leaving it (patch released, globe shown). The release path in
`UpdateTerrainPatch` has never been exercised with a visible patch.

## 87 — Instrument the live patch path

**In progress.** Code is done and green; the report itself is unproven until a playtest shows it
firing. Deliberately not closed on a passing suite: an automated run renders nothing, so it cannot
tell whether `ReportPatchIfPending` ever executes.

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

**Pending.**

Both the globe and the ground patch are created `NoCollision`. The design is that contact comes from
`FPlanetTerrain` rather than from either mesh, because the dedicated server holds no mesh and must
still agree about where the ground is — so the mesh can never be the authority on it.

Worth establishing what currently decides a ship has landed: `ClientA.log` at 15:05:33 shows
`Touched down` and `Lifted off` alternating four times within 0.3 s, which looks like a missing
hysteresis and may deserve its own task once understood.

## 89 — Caves need a second representation

**Pending.** Long-term.

A height field is a function of direction and cannot express an overhang, let alone a cave. Whatever
is added must keep the property that makes the current model work: the server and every client agree
about the ground without shipping any of it (ADR-0002).

---

## Done

Nothing yet under this file's numbering.
