---
name: blender-greybox
description: Build a verified greybox (blockout) in Blender from a design document — an architectural plan, orthographic sheet, concept drawing, spec table or design artifact — and export it to a game engine. Produces a regenerable Python build script, a .blend, an engine-ready FBX with collision, and preview renders, plus automated checks for scale, z-fighting and character clearance. Use this whenever someone wants something modelled, blocked out, greyboxed, whiteboxed or prototyped in Blender from a drawing, plan, sheet, artifact or spec — stations, buildings, interiors, ships, vehicles, props or character proxies — and whenever they mention Blender, greybox, blockout, .blend, or getting geometry into Unreal or Unity. Also use it when an existing greybox needs fixing, re-measuring, or regenerating after the source changed.
---

# Greyboxing from a design document

You are turning a drawing into geometry someone will walk around in. The work
is mostly transcription, and the interesting part is that **every way this goes
wrong is invisible in the drawing**. A corridor that no character can enter, a
floor that flickers, a building at 1/100th scale — all of them look perfect on
the sheet and perfect in the source numbers. They only show up when you measure
the mesh you actually built.

So the discipline is: generate the model from a script, and make the script
assert things about its own output.

## What you produce

- **A build script** — the model as code, checked into the project. The numbers
  live here, not in the `.blend`. Anyone can regenerate it; nobody hand-edits
  the mesh, because a hand edit is a number that no longer traces to the source.
- **A `.blend`** for looking at and iterating on.
- **An engine file** (usually FBX) with collision hulls.
- **Preview renders** so the person who asked can judge it without opening
  Blender.

Bundled with this skill: `scripts/greybox_lib.py` holds the geometry
primitives, all four checks, collision generation, the render setup and the
FBX export. Copy it next to your build script and import it — it exists so you
spend your effort on the transcription rather than rediscovering that an area
light in frame blows out the shot.

## Workflow

### 1. Read the source and pin the scale

Read the whole design document first, including notes and schedules — they
usually contain the constraints that decide the geometry, and sometimes they
say the drawing is superseded or wrong.

Then find the scale, and derive it rather than assuming it. Drawings carry
dimension lines; SVG artifacts carry coordinates you can measure against a
stated dimension. If a 40 m dimension spans x=30..430, the scale is 10 units
per metre.

**Confirm it with a second, independent dimension before trusting it.** A stated
door width or a table entry that falls out exactly at the derived scale is the
proof. One number agreeing is a coincidence; two is a scale.

### 2. Write down what the source claims

Most design documents publish numbers: a room schedule, a parts table, counts,
overall dimensions. Collect them, because they are your test oracle. Ask the
user for the equivalent if the source has none — proportions, key dimensions,
anything checkable.

### 3. Check what the geometry has to fit

Find the real dimensions of whatever will interact with this model, from the
codebase rather than from memory. For anything walkable that means the
character's collision capsule — grep for capsule radius, character height, or
the movement component's settings. For a ship interior it might be the docking
volume; for a character proxy, the rig proportions.

This matters because **plans are dimensioned to centrelines, and that is not
the space anybody walks in**. Two walls a metre apart on a drawing leave half a
metre of air once each takes its thickness, and a typical character capsule is
0.6–0.7 m across. Nothing about the drawing looks wrong.

### 4. Build it

Write the build script. State geometry in the source's own coordinates so a
reader can hold the script and the drawing side by side — `greybox_lib`'s
`to_local()` maps plan coordinates to Blender, so you never bake the transform
into the numbers.

Two conventions worth keeping unless the project says otherwise:

- **The model does not know where it is.** Origin at the placement point,
  floor at Z=0, no world position anywhere in the file. Where a thing stands is
  content; the mesh only describes local geometry.
- **Solids overlap, they never meet exactly.** See the next section.

Comment each solid with the source feature it came from — the SVG path, the
table row, the callout. That comment is what makes a mis-transcription findable
later.

### 5. Run the checks, and drive them to zero

The four checks are in `greybox_lib`. Run them before rendering anything:

```
check_schedule(measured, published)        # agrees with the source's own numbers
report_coincident_faces()                  # -> 0 across two materials
report_tight_gaps(...)                     # -> 0 narrower than the character
report_bounds(objects)                     # the envelope is what you expect
```

Wire a `--check-only` flag that runs them and exits non-zero on failure, so the
build refuses rather than reports.

### 6. Render and actually look

Renders are the only check for the things no assertion covers: whether it reads
as the thing it is meant to be, whether the space feels right, whether a
reveal lands. Produce a top-down plan per level, a cutaway, and eye-level shots
from where a person arrives.

**Then open the images and look at them.** Numbers passing is not the same as
the model being right, and a render nobody looked at is worth nothing.

### 7. Report deviations explicitly

The source will be internally inconsistent somewhere — a wall drawn 0.8 m from
where it should stack, a schedule that disagrees with a drawn rectangle, an
element with no stated height. Some of it you must resolve to build at all.

Say which choices were yours, and why, in the same breath as delivering. A
silently corrected drawing is worse than a flagged one, because the next person
compares the model to the sheet and finds a discrepancy nobody recorded.

If the fix belongs in the design document rather than the model — a rule that
would prevent the same fault on the next sheet — offer to put it there.

## The one that will bite you: coincident faces

Building a plan naively means every junction meets exactly. Wall tops land on
the floor slab above them, slab edges are cut to the wall faces they abut,
furniture stands on the floor surface. All geometrically correct, and all of it
flickers: two coplanar faces pointing the same way at the same depth have
nothing to break the tie, so the renderer picks per pixel per frame.

**The fix is not to nudge faces by hand. It is to make solids overlap.** Pick a
knit distance (0.1 m works well at building scale) and apply it everywhere:

- Wall tops end *inside* the slab above, not on it.
- Slab edges end *inside* the walls they meet, not flush with them.
- Anything standing on a floor starts *below* its surface.
- Lintels reach into the walls either side.
- The roof soffit dips below every wall top so it swallows them.

Nothing looks different, because a face buried inside another solid was never
visible. `report_coincident_faces()` grades what remains: pairs sealed inside a
third solid cannot be seen, pairs sharing a material tie without flickering,
and pairs across two materials are the ones that show. Drive that last number
to zero.

Two related traps, both of which produce a coincident face that no knit
distance fixes because the geometry itself is wrong:

- **A stair whose last tread is the floor it lands on.** Draw one fewer tread
  and let the landing slab be the last one; the final riser is the step up onto
  it. `greybox_lib.stair()` does this.
- **A lintel over an opening whose head is already the ceiling.** If the head
  height equals the slab soffit, there is no lintel — the slab is the head.

## Adapting this beyond floor plans

The four checks are a pattern, not a fixed list. What stays constant is: derive
the scale, assert against the source's published numbers, eliminate coincident
faces, and check the model against the real dimensions of whatever interacts
with it.

- **Ships and vehicles** — orthographic views instead of plans. Scale comes
  from a stated overall length. The clearance check becomes internal headroom
  and hatch widths against the character, plus the external envelope against
  whatever has to hold it: a hangar, a landing pad, a docking collar.
- **Characters and creatures** — scale from a stated height. The schedule check
  becomes proportion ratios against the reference. Clearance runs the other way:
  the character must fit through the doors that already exist. If the target
  engine has a skeleton convention, check the proxy's proportions against it
  before anyone rigs to it.
- **Props and modular kits** — the schedule check becomes grid conformance. If
  the project has a modular kit, snap to its unit so pieces interchange, and
  assert that every piece is a whole number of units.

## Reference files

- `references/verification.md` — how each check works, what its output means,
  and how to adapt it to a new subject. Read when a check reports something you
  do not recognise, or when you need to write a fifth one.
- `references/blender-api.md` — headless Blender: building meshes without
  `bpy.ops`, render engine names and settings, why interiors need EEVEE with
  ambient rather than Workbench, camera framing. Read before writing the render
  section.
- `references/unreal.md` — naming collisions between meshes and materials,
  `UCX_` collision hulls and why a mesh without them is intangible rather than
  roughly solid, FBX scale, pivots. Read when the target is Unreal.

## Where the outputs go

Ask, or follow the project's convention. A useful default: the working files
(`.blend`, renders) live outside the game repository, and only the engine file
is committed — a `.blend` and a folder of PNGs are working material, not a
build input. `parse_args()` takes `--out` and `--fbx` separately so the split
is enforced rather than remembered.
