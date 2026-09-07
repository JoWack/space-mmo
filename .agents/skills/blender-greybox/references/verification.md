# The checks, and how to adapt them

Each of these exists because the fault it catches is invisible in the drawing
and invisible in the source numbers. They are worth understanding rather than
copying, because a new subject needs the same *shape* of check with different
content.

## 1. Scale

Not a function — a derivation you do once and then prove.

Find a dimension the source states and measure what it spans in the source's
own units. Then find a **second, independent** dimension and check it falls out
at the same scale. A door width, a table entry, an overall envelope: if the
derived scale reproduces it exactly, the scale is right. One agreement is a
coincidence.

If the second number does not agree, stop. Either you have misread a dimension
line or the drawing is not to a single scale, and both are worth knowing before
you build 900 solids on the wrong assumption.

## 2. Schedule — does the model agree with the source's own numbers?

`check_schedule(measured, published)` compares quantities you compute from your
constants against numbers the source prints, and raises rather than warns.

Compute the measured side from the same constants the geometry uses, so a
mis-transcribed coordinate moves both the model and the measurement and the
comparison still catches it. Do not hardcode the measured value — that turns
the check into a restatement of the answer.

What to compare, by subject:

- **Buildings** — room areas, gross area per level, counts of repeated elements
  (terminals, bays, alcoves).
- **Ships** — overall length, beam, deck heights, engine and hardpoint counts.
- **Characters** — total height, and proportion ratios (head height, shoulder
  width over height) against the reference.
- **Kits** — every piece a whole number of grid units.

Counts matter as much as dimensions: they catch a loop that ran one time too
many, which no area check notices.

## 3. Coincident faces — will it flicker?

`report_coincident_faces()` walks every pair of solids and finds same-facing
coplanar faces with overlapping area.

It reports three categories, and only one is a fault:

- **Sealed inside a third solid** — cannot be seen, so cannot flicker. Wall
  tops meeting under a slab are the usual case.
- **Same material both sides** — both candidates shade identically, so the
  depth test ties without any visible artefact. Butt joints between two wall
  segments of the same material live here.
- **Across two materials** — the ones that show, because two different colours
  compete for the same pixels. **Drive this to zero.**

Grading by material is what makes the number meaningful. Without it a healthy
model reports a few hundred "faults" and the real ones are lost.

The fix is always to change how solids meet, never to nudge a face. See the
knit rule in SKILL.md.

### If it still flickers after the count is zero

The check only understands axis-aligned solids. Coplanar faces from cylinders,
rotated geometry or imported parts will not be caught. Look for two objects
that share a plane by construction — a decal on a wall, a floor inlay at
exactly floor height — and sink one into the other.

## 4. Clearance — does the character fit?

`report_tight_gaps(floor_z, level, pawn_diameter, pawn_height, min_walkway,
obstacle_materials)` finds solids facing each other across a gap within the
band a standing character occupies.

Get the character's real dimensions from the codebase, not from memory. A
capsule radius of 34 cm means a 0.68 m character, and it is the diameter that
matters.

Two thresholds, and the difference is the point:

- **Below the character's width** is not a corridor. It is a wall with a gap in
  it, and the route it appears to offer does not exist. Zero of these.
- **Below a working minimum** (roughly the character plus a hand either side —
  1.2 m at human scale) is somewhere a player scrapes along and the camera
  fights the geometry. Fix these too unless there is a reason not to.

Pass only materials that obstruct walking. Floors, slabs, roofs and stair
treads are surfaces a character stands on or under; counting a tread as an
obstacle reports a staircase as two dozen impassable slots and buries the real
findings.

**Furniture goes flush to its wall or a full walkway clear of it.** A plan's
rectangles often sit a quarter metre off the wall behind them, which makes a
slot nobody can enter and nobody can see the back of. It is not a route, so it
is easy to dismiss, but it is somewhere a character can wedge.

### Adapting it

- **Ship interiors** — same check, plus hatch and companionway widths.
- **Characters** — runs the other way: the character must fit the doors that
  already exist. Check the proxy's width against the narrowest route it needs.
- **Vehicles and props** — the external envelope against whatever holds it: a
  hangar, a berth, a landing pad, a rack.

## 5. Bounds — is it the size you think?

`report_bounds(objects)` prints the built envelope. Compare it to the source's
stated overall dimensions every run.

This catches the cheapest possible mistake: a roof oversail, a stray solid, or
a coordinate typo that moves one wall a hundred metres. It also catches
something subtler — a change made to fix a rendering fault that quietly
resized the whole model. Fixing a flicker is not licence to change the
building, and the bounds line is what tells you it happened.
