# Getting a greybox into Unreal

## Names collide across types

Unreal imports meshes and materials into one namespace. A material called
`Roof` takes the name, and the mesh called `Roof` arrives second and is quietly
renamed `Roof1`.

That is not cosmetic. Collision is matched **by name**, so `UCX_Roof_01` then
matches a mesh that no longer exists under that name and attaches to nothing —
and an intangible roof looks exactly like a roof until someone walks through it.

Prefix materials (`MAT_`) or otherwise guarantee the sets are disjoint, and
assert it in the build script rather than trusting anyone to remember.
`greybox_lib.check_names_are_distinct()` does this and fails the run.

## Collision has to be authored, or there is none

`UCX_<meshname>_NN` objects in the FBX import as simple collision, one convex
hull each. The suffix matters: the importer matches the mesh by the name
between the prefix and it.

A walking character sweeps against **simple** collision only. The engine treats
that as a choice between simple and complex, not a preference, so a mesh with
no simple collision is not roughly solid — it is intangible. Walking through a
wall reads as a bug in the movement code, which is an expensive place to start
looking.

Enable *Import Collision* on the import, and check the result: the static mesh
editor shows the hull count, and it should match what the build script printed.

Two things that make this cheap:

- **An axis-aligned box is already a convex hull.** If the greybox is boxes,
  the collision is the model rather than an approximation of it. No
  decomposition, nothing to tune.
- **Generate collision from the same list the geometry came from**, in the same
  run. Then the two cannot drift. Read that list rather than appending to it,
  or the coincidence check will report the collision as coincident with the
  geometry that produced it.

Stairs are the exception worth handling: a flight of individual box hulls is
something a character catches on. Merge the flight into one sloped hull —
`greybox_lib.collision_hulls(ramp_groups=...)` takes a run direction per stair
group and emits a ramp.

## Scale

Export with `apply_scale_options="FBX_SCALE_UNITS"` and the scene in metres.
That is what makes one metre in Blender one metre — 100 uu — in Unreal. Without
it the usual symptom is a model imported at 1/100th or 100× size.

Verify by reading the imported mesh's bounds in the static mesh editor rather
than by looking at it in a level, where a 40 m building and a 0.4 m building
both look plausible next to nothing.

## Pivots and placement

Keep the origin at the point the thing is placed from, floor at Z=0, and put no
world position in the file. Where a thing stands is content — a transform in a
level, a row in a data table, a direction in a content pack — and a mesh that
also remembered a position would be a second answer to "where is it".

If the project resolves a mesh by kind or key and then *fits* it to a
configured size, know which path this asset takes. A Blueprint assembled from
parts is usually drawn at its authored size and never fitted; a bare static
mesh may be scaled to a configured extent, in which case the mesh bounds decide
the result and an unintended roof overhang changes the whole building's size.

## After import, check rather than assume

- Bounds match what the build script reported.
- Hull count matches.
- Materials came in as separate slots, not merged.
- Walk it. Collision, clearance and scale are all things the editor will show
  you in thirty seconds and no headless check can confirm.
