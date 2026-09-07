# Borlash City - A-07 exterior blockout

Open `D:/Documents/SpaceMMOAssets/Blender/Stations/BorlashCity/BorlashCity.blend`.
The complete assembled city is in that file, in Core, Trading, Civil, Crafting
and Refining collections. Collision collections are hidden in the viewport.
Station_Anchors identifies all 17 proposed sites; Review_Cameras includes an
overview, north-up plan, trading quarter and two 1.7m eye-level cameras.

## Source and interpretation

Origin Station Plans.pdf, pages 21-30, controls the layout and station schedule.
Its corresponding A-07 SVG was available from the earlier session and checked
against the rendered PDF: 400 SVG units across the walls equals 400m; 584 across
the docks equals 584m; the Market Hall is independently 72 x 44m.
CapitalGrandDistrict.png supplies domes, metallic roof bands, engaged buttresses
and clustered HQ spires. The PDF's clipped-square wall and circular ring road
are retained, rather than replacing the plan with the concept's round wall.

The city has 17 sites, four external docks, four cardinal gates, four corner
posterns, 16m avenues, a nominal 12m ring road and a 48 x 48m square. Wall
centrelines are 400 x 400m; overall dock bounds are 584 x 560m. Walls reach 12m,
gate towers 20m, HQ 45m. Other heights and facade forms are blockout assumptions.

The hotel uses the table's 40 x 26m instead of the drawing's 38 x 26m.
Gate towers interrupt the drawn ring centreline, so 12m bypass lanes turn behind
them. Dock back walls have 12m openings to keep both southern docks accessible.
Ground and paving intersections are partitioned into non-overlapping surfaces:
even equal-material overlapping faces caused black seams in Cycles previews.

This is the exterior shell phase: building masses are deliberately closed,
entrance panels mark future doors, and service interiors are not implemented.
No NPC supplies, finance mechanics, quest chains, or global market behavior was
inferred from labels in the older art/PDF. The current design bible supersedes
the old thruster-sales note. `station_capital_hub` remains the administration
marker; no station keys or gameplay data were changed.

## Regenerate

From the repository, in PowerShell:

```powershell
& 'G:/SteamLibrary/steamapps/common/Blender/blender.exe' --factory-startup --background --python-exit-code 1 --python tools/greybox/a07_borlash_city.py
```

Append `-- --check-only` for geometry and clearance checks without saving files.
Append `-- --verify-exports-only` to re-import the existing five FBXs and check
bounds, scale, names and collision pairing. A normal build performs both checks.
`--out` overrides the external working folder; `--engine-out` overrides this
export directory. The generator refuses to write working assets inside the repo.

## Engine import

There are five FBXs, not one indivisible city mesh. Each district is exported
relative to its own anchor; `Borlash_manifest.json` records the city-local metre
translations needed to reconstruct the city. These are Blender coordinates,
+Y north and +Z up. Respect the import coordinate conversion when assembling in
Unreal. Import at true size; do not fit this city to Capital's 40m placeholder.
Use Import Collision and one convex hull per UCX object; keep material names.
Preserve/bake FBX node transforms consistently, then apply each district anchor
once. Compare with the assembled .blend and the manifest before saving a prefab.

## Verification and limits

The generator measures built station plates and heights, checks exact duplicate
cross-material polygons and distinct asset names, and flood-fills ground routes
from the convex collision geometry. A 1m grid is conservatively inflated by
0.60m plus the cell diagonal, so reachable cells provide at least 1.20m of route
width for the current 0.68m-wide, 1.80m-high pawn. All 17 frontages, four gates,
and the complete ring (including bypasses) are reachable. The inside of the
market is a negative control and correctly fails accessibility.

FBX roundtrip checks all 129 render meshes and 603 named convex hulls, with
maximum bounds/anchor error below 0.05mm. This is a Blender re-import check,
not proof of Unreal import settings or gameplay collision. See the manifest
for per-district counts and measurements. The coincidence check is not an
exhaustive proof for every partially overlapping polygon; the final five
renders were also inspected. Streets have no step-up curbs.

Next manual pass: import and align the districts, walk from each dock through
its postern or gate to the square, circle the gate bypasses, and orbit street
intersections for flicker. The 1m grid route distances are conservative Manhattan
estimates, not confirmation of the PDF's 85-second travel claim. Terrain
flattening and gameplay station placement remain separate work under task 97;
the city foundation is a local flat reference, not a solution for planet relief.

Nothing has been committed or pushed by this build.
