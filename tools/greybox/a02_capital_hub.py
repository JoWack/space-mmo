#!/usr/bin/env python
"""
Greybox for sheet A-02 "Capital Hub" of the Origin Station Plans.

Run headless:

    blender --background --python tools/greybox/a02_capital_hub.py -- --out <dir>

Every number here is transcribed from the A-02 plan SVG. The sheet's own 40 m
dimension line runs across SVG x=30..430, which fixes the scale at
10 SVG units = 1 metre; Level 01 is drawn at the same scale offset to x=490.

Plan coordinates used throughout this file:

    x   0 -> 40 m, west to east
    y   0 -> 40 m, north to south      (as drawn: SVG y increases southward)
    z   0 m at the Level 00 floor, up

Dimensions are to wall centrelines, as the sheet's conventions panel states,
so the building measures 40.5 m over its outer faces.

Blender coordinates are centred on the footprint with +Y north, floor at Z=0.
The object origin is therefore the point the station is placed at, and nothing
in the file records a world position -- task 124's rule that a station prefab
must not know where it is.
"""

import math
import os
import sys

import bpy
from mathutils import Vector

# --------------------------------------------------------------------------
# Dimensions read off the sheet
# --------------------------------------------------------------------------

FOOTPRINT = 40.0            # title block: 40 x 40 m
T_STRUCT = 0.5              # conventions: structural wall, 0.5 m nominal
T_PART = 0.2                # conventions: partition, non-structural

H_L00_CLEAR = 4.0           # note 6: "4 m elsewhere"
T_SLAB = 0.5
Z_L01 = H_L00_CLEAR + T_SLAB          # 4.5 -- top of the Level 01 slab
Z_ROOF = 9.0                # note 6: "Ceiling 9 m over the trade floor"
T_ROOF = 0.5

# Door and opening widths are all read from gaps in the drawn wall paths.
# Head heights are NOT on the sheet -- these are the assumption. See report.
H_DOOR = 3.0                # ordinary 3 m doorways
H_MAIN = 3.5                # main entrance (6 m wide) and east door (5 m)
H_OPENING = 4.0             # the 8 m arrival hall -> trade floor opening

# Service element heights, also not on the sheet (it is a plan). Greybox
# values chosen so each part reads as the piece of furniture it is.
H_COUNTER = 1.1             # F faction supply, R registry
H_TERMINAL = 1.2            # M market terminal
H_QUEST = 1.2               # Q quest stand
H_RACK = 2.5                # H hangar racking
H_RAIL = 1.1                # gallery parapet
H_ALCOVE_FRONT = 1.1        # see report -- the one place the plan is ambiguous

PLATE_R_OUTER = 3.6
PLATE_R_INNER = 2.4

STAIR_STEPS = 26            # 4.5 m rise over a 9 m run = 26.6 degrees
STAIR_DRAWN = STAIR_STEPS - 1   # the slab itself is the last tread

# --------------------------------------------------------------------------
# Knitting the solids together
#
# The first build met every junction exactly: wall tops landed on the Level 01
# slab top at 4.5, slab edges sat on the wall faces they were cut to, and
# everything standing on a floor started at its surface. All correct, and all
# unrenderable -- two coplanar faces pointing the same way at the same depth
# have nothing to break the tie, so they flicker. 155 such pairs, worst of
# them across the whole Level 01 floor.
#
# The rule now is that solids overlap rather than touch. A face buried inside
# another solid cannot fight with anything, and the union looks identical.
# report_coincident_faces measures it, so this cannot quietly come back.
# --------------------------------------------------------------------------

KNIT = 0.1                          # how far solids reach into each other

Z_SUNK = -KNIT                      # floors: sit things inside the ground slab
Z_SLAB_BOT = Z_L01 - T_SLAB         # 4.0
Z_L00_WALL_TOP = Z_L01 - 0.20       # 4.30, buried in the Level 01 slab
Z_L01_WALL_BOT = Z_L01 - 0.30       # 4.20, buried, and clear of the above
Z_RAIL_BOT = Z_L01 - 0.15           # 4.35, buried, and clear of both
Z_ROOF_BOT = Z_ROOF - KNIT          # 8.90, so it swallows every wall top

# --------------------------------------------------------------------------
# Materials and grouping
# --------------------------------------------------------------------------

# Prefixed, and not decoratively.
#
# <strong>Unreal imports meshes and materials into one namespace.</strong> A material called GB_Roof
# takes that name, and the mesh called GB_Roof arrives second and is quietly renamed GB_Roof1 --
# which is how the roof ended up with no collision on it: UCX_GB_Roof_01 matched a mesh that no
# longer existed under that name, and an intangible roof looks exactly like a roof.
#
# Only that one name ever collided, but the rule is what matters: nothing here may be called the
# same thing as a mesh group, and check_names_are_distinct below fails the run rather than trusting
# anybody to remember.
MATERIALS = {
    "MAT_GB_Structure": (0.52, 0.54, 0.55, 1.0),
    "MAT_GB_Partition": (0.71, 0.73, 0.74, 1.0),
    "MAT_GB_Floor":     (0.38, 0.40, 0.41, 1.0),
    "MAT_GB_Service":   (0.09, 0.39, 0.37, 1.0),   # the sheet's accent, #16635F
    "MAT_GB_Stair":     (0.60, 0.62, 0.63, 1.0),
    "MAT_GB_Roof":      (0.30, 0.32, 0.33, 1.0),
}

# group name -> (collection, material)
GROUPS = {
    "GB_L00_Floor":       ("L00_Structure", "MAT_GB_Floor"),
    "GB_L00_Walls":       ("L00_Structure", "MAT_GB_Structure"),
    "GB_L00_Partitions":  ("L00_Structure", "MAT_GB_Partition"),
    "GB_L00_Service":     ("L00_Service",   "MAT_GB_Service"),
    "GB_L00_Plate":       ("L00_Service",   "MAT_GB_Partition"),
    "GB_L00_Stair_West":  ("L00_Stairs",    "MAT_GB_Stair"),
    "GB_L00_Stair_East":  ("L00_Stairs",    "MAT_GB_Stair"),
    "GB_L01_Slab":        ("L01_Structure", "MAT_GB_Floor"),
    "GB_L01_Walls":       ("L01_Structure", "MAT_GB_Structure"),
    "GB_L01_Fronts":      ("L01_Structure", "MAT_GB_Partition"),
    "GB_L01_Railing":     ("L01_Structure", "MAT_GB_Partition"),
    "GB_Roof":            ("Shell",         "MAT_GB_Roof"),
}


def check_names_are_distinct():
    """Nothing exported may share a name with anything else exported.

    Cheap, and it has already been worth it once: the roof mesh and the roof
    material were both GB_Roof, so the engine renamed the mesh on import and
    its collision, which is matched by name, attached to nothing.
    """
    clash = sorted(set(GROUPS) & set(MATERIALS))

    if clash:
        raise SystemExit(
            "mesh groups and materials share a name, which Unreal resolves by "
            "renaming one of them on import: %s" % ", ".join(clash))

_geometry = dict((name, {"verts": [], "faces": []}) for name in GROUPS)

# Every solid, in plan coordinates, kept so the model can be measured for
# coincident faces after it is built. See report_coincident_faces.
_boxes = []


def _plan_to_blender(x, y):
    """Plan metres -> Blender metres, centred, +Y north."""
    return (x - FOOTPRINT / 2.0, FOOTPRINT / 2.0 - y)


def box(group, x0, x1, y0, y1, z0, z1, label=""):
    """An axis-aligned solid given in plan coordinates."""
    _boxes.append((group, label, (x0, x1, y0, y1, z0, z1)))
    g = _geometry[group]
    base = len(g["verts"])
    bx0, by1 = _plan_to_blender(x0, y0)
    bx1, by0 = _plan_to_blender(x1, y1)
    g["verts"].extend([
        (bx0, by0, z0), (bx1, by0, z0), (bx1, by1, z0), (bx0, by1, z0),
        (bx0, by0, z1), (bx1, by0, z1), (bx1, by1, z1), (bx0, by1, z1),
    ])
    for f in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
              (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        g["faces"].append(tuple(base + i for i in f))


def cylinder(group, cx, cy, radius, z0, z1, segments=48):
    g = _geometry[group]
    base = len(g["verts"])
    bcx, bcy = _plan_to_blender(cx, cy)
    for z in (z0, z1):
        for i in range(segments):
            a = 2.0 * math.pi * i / segments
            g["verts"].append((bcx + radius * math.cos(a),
                               bcy + radius * math.sin(a), z))
    for i in range(segments):
        j = (i + 1) % segments
        g["faces"].append((base + i, base + j,
                           base + j + segments, base + i + segments))
    g["faces"].append(tuple(base + i for i in range(segments - 1, -1, -1)))
    g["faces"].append(tuple(base + segments + i for i in range(segments)))


def wall_ns(group, x, y0, y1, z0, z1, t=T_STRUCT, label=""):
    """Wall running north-south, centreline at x."""
    box(group, x - t / 2.0, x + t / 2.0, y0, y1, z0, z1, label)


def wall_ew(group, y, x0, x1, z0, z1, t=T_STRUCT, label=""):
    """Wall running east-west, centreline at y."""
    box(group, x0, x1, y - t / 2.0, y + t / 2.0, z0, z1, label)


AXIS_NAME = ("x", "y", "z")


def _enclosed(ax, plane, rect, exclude):
    """Is this patch strictly inside some third solid?

    Two coplanar faces sealed inside a third box cannot be seen, so they
    cannot flicker. Wall tops meeting under a slab are the common case, and
    counting them as faults would make the number meaningless.
    """
    for k, (_g, _l, b) in enumerate(_boxes):
        if k in exclude:
            continue
        if not (b[ax * 2] < plane - 1e-6 < b[ax * 2 + 1] - 1e-6):
            continue
        inside = True
        for other in range(3):
            if other == ax:
                continue
            lo, hi = rect[other]
            if b[other * 2] > lo + 1e-6 or hi > b[other * 2 + 1] + 1e-6:
                inside = False
                break
        if inside:
            return True
    return False


# --------------------------------------------------------------------------
# Does a person fit?
#
# ASpaceMMOCharacterPawn sweeps a capsule of CollisionRadiusCentimetres = 34
# and stands CharacterHeightCentimetres = 180 tall
# (client/Source/SpaceMMOCore/Public/SpaceMMOCharacterPawn.h). So the pawn is
# 0.68 m across, and anything narrower than that it cannot enter at all.
#
# A plan drawn to wall centrelines hides this: two walls 1.0 m apart on the
# drawing leave 0.5 m of air once they are 0.5 m thick, and a 0.68 m pawn
# does not fit through 0.5 m. Nothing about the drawing looks wrong.
# --------------------------------------------------------------------------

PAWN_DIAMETER = 0.68        # 2 x CollisionRadiusCentimetres
PAWN_HEIGHT = 1.80          # CharacterHeightCentimetres
MIN_WALKWAY = 1.20          # the pawn plus room either side, not a squeeze


def report_tight_gaps(floor_z, level, limit=12):
    """Find places where two solids face each other too closely to walk between.

    Everything here is axis aligned, so a pinch point is a pair of solids that
    overlap along one horizontal axis and leave a gap along the other, within
    the band a standing character occupies.
    """
    # Only things that obstruct walking count. Floors, slabs, roofs and stair
    # treads are surfaces a character stands on or under, and counting a tread
    # as an obstacle reports a staircase as 25 impassable slots.
    obstacles = ("MAT_GB_Structure", "MAT_GB_Partition", "MAT_GB_Service")
    lo, hi = floor_z + 0.05, floor_z + PAWN_HEIGHT
    live = [(g, l, b) for g, l, b in _boxes
            if GROUPS[g][1] in obstacles
            and b[4] < hi - 1e-6 and b[5] > lo + 1e-6]

    gaps = []
    for i in range(len(live)):
        gi, li, bi = live[i]
        for j in range(i + 1, len(live)):
            gj, lj, bj = live[j]
            for ax, other in ((0, 1), (1, 0)):
                run = min(bi[other * 2 + 1], bj[other * 2 + 1]) - \
                    max(bi[other * 2], bj[other * 2])
                if run < 0.5:            # not facing each other across a gap
                    continue
                if bi[ax * 2 + 1] <= bj[ax * 2]:
                    gap = bj[ax * 2] - bi[ax * 2 + 1]
                elif bj[ax * 2 + 1] <= bi[ax * 2]:
                    gap = bi[ax * 2] - bj[ax * 2 + 1]
                else:
                    continue             # they overlap; no gap to measure
                if 0.01 < gap < MIN_WALKWAY - 1e-6:
                    gaps.append((gap, run, AXIS_NAME[ax], gi, li, gj, lj))

    gaps.sort(key=lambda g: g[0])
    print("")
    print("  %s: gaps under %.2f m between facing solids" % (level, MIN_WALKWAY))
    if not gaps:
        print("    none")
        return 0
    blocked = sum(1 for g in gaps if g[0] < PAWN_DIAMETER)
    for gap, run, ax, gi, li, gj, lj in gaps[:limit]:
        mark = "IMPASSABLE" if gap < PAWN_DIAMETER else "tight"
        print("    %5.2f m clear over %5.1f m of %s  %-10s  %s vs %s"
              % (gap, run, ax, mark, li or gi, lj or gj))
    if len(gaps) > limit:
        print("    ... and %d more" % (len(gaps) - limit))
    print("    %d narrower than the %.2f m pawn" % (blocked, PAWN_DIAMETER))
    return blocked


def report_coincident_faces(limit=14):
    """Find pairs of solids that put two same-facing faces on the same plane.

    That is what z-fights: two coplanar faces pointing the same way at the
    same depth, with nothing to break the tie. Solids that interpenetrate are
    fine, and solids that merely abut hide their shared plane inside the
    union. Same-side coincidence is the bug, and it is invisible in the
    source -- every one of these boxes is at the coordinate the plan gives.
    """
    found = []
    buried = [0]
    same_material = [0]
    n = len(_boxes)
    for i in range(n):
        gi, li, bi = _boxes[i]
        for j in range(i + 1, n):
            gj, lj, bj = _boxes[j]
            for ax in range(3):
                for side, off in (("min", 0), ("max", 1)):
                    plane = bi[ax * 2 + off]
                    if abs(plane - bj[ax * 2 + off]) > 1e-6:
                        continue
                    area = 1.0
                    for other in range(3):
                        if other == ax:
                            continue
                        lo = max(bi[other * 2], bj[other * 2])
                        hi = min(bi[other * 2 + 1], bj[other * 2 + 1])
                        if hi - lo <= 1e-4:
                            area = 0.0
                            break
                        area *= hi - lo
                    if area <= 0.0:
                        continue
                    rect = []
                    for other in range(3):
                        if other == ax:
                            rect.append(None)
                        else:
                            rect.append((max(bi[other * 2], bj[other * 2]),
                                         min(bi[other * 2 + 1],
                                             bj[other * 2 + 1])))
                    if _enclosed(ax, plane, rect, (i, j)):
                        buried[0] += 1
                        continue
                    if GROUPS[gi][1] == GROUPS[gj][1]:
                        # Both faces shade identically, so whichever the
                        # renderer picks looks the same. It ties; it does
                        # not flicker.
                        same_material[0] += 1
                        continue
                    found.append((area, AXIS_NAME[ax], side, plane,
                                  gi, li, gj, lj))
    found.sort(reverse=True, key=lambda f: f[0])
    print("")
    print("  coincident, buried inside a third solid: %d (cannot be seen)"
          % buried[0])
    print("  coincident, same material on both faces: %d (ties, does not flicker)"
          % same_material[0])
    if not found:
        print("  coincident across two materials: none")
        return 0
    print("  coincident across two materials: %d pair(s), worst first"
          % len(found))
    for area, ax, side, plane, gi, li, gj, lj in found[:limit]:
        print("    %8.2f m2  %s=%-7.3g %s  %s/%s  vs  %s/%s"
              % (area, ax, plane, side, gi, li or "-", gj, lj or "-"))
    if len(found) > limit:
        print("    ... and %d more" % (len(found) - limit))
    return len(found)


# --------------------------------------------------------------------------
# Level 00 -- arrival and trade
# --------------------------------------------------------------------------

OUT = FOOTPRINT + T_STRUCT / 2.0    # 40.25, outer face of the perimeter
NEG = -T_STRUCT / 2.0               # -0.25

W = "GB_L00_Walls"
P = "GB_L00_Partitions"
S = "GB_L00_Service"

# Ground slab. Its edges stop short of the perimeter's outer face so they sit
# inside the wall rather than flush with it.
box("GB_L00_Floor", NEG + KNIT, OUT - KNIT, NEG + KNIT, OUT - KNIT,
    -T_SLAB, 0.0, "ground slab")

# Perimeter. SVG: M 30 30 H 430 V 200 M 430 250 V 430 H 260 M 200 430 H 30 V 30
# -> east door gap y 17..22 (5 m), south entrance gap x 17..23 (6 m).
# Wall tops stop at Z_ROOF, which the roof underside at Z_ROOF_BOT swallows.
wall_ew(W, 0.0, NEG, OUT, Z_SUNK, Z_ROOF, label="perimeter N")
wall_ew(W, FOOTPRINT, NEG, 17.0 + KNIT, Z_SUNK, Z_ROOF, label="perimeter S/W")
wall_ew(W, FOOTPRINT, 23.0 - KNIT, OUT, Z_SUNK, Z_ROOF, label="perimeter S/E")
wall_ew(W, FOOTPRINT, 17.0, 23.0, H_MAIN, Z_ROOF, label="main entrance head")
wall_ns(W, 0.0, T_STRUCT / 2.0, FOOTPRINT - T_STRUCT / 2.0, Z_SUNK, Z_ROOF,
        label="perimeter W")
wall_ns(W, FOOTPRINT, T_STRUCT / 2.0, 17.0 + KNIT, Z_SUNK, Z_ROOF,
        label="perimeter E/N")
wall_ns(W, FOOTPRINT, 22.0 - KNIT, FOOTPRINT - T_STRUCT / 2.0, Z_SUNK, Z_ROOF,
        label="perimeter E/S")
wall_ns(W, FOOTPRINT, 17.0, 22.0, H_MAIN, Z_ROOF, label="east door head")

# North band at y=8, two offices. SVG: M 30 110 H 100 M 130 110 H 330 M 360 110 H 430
# -> 3 m doors at x 7..10 and x 30..33. Partition on the centre line at x=20.
# Interior walls die on the perimeter centrelines, a quarter-metre inside it.
wall_ew(W, 8.0, 0.0, 7.0 + KNIT, Z_SUNK, Z_L00_WALL_TOP, label="north band W")
wall_ew(W, 8.0, 10.0 - KNIT, 30.0 + KNIT, Z_SUNK, Z_L00_WALL_TOP, label="north band C")
wall_ew(W, 8.0, 33.0 - KNIT, FOOTPRINT, Z_SUNK, Z_L00_WALL_TOP, label="north band E")
wall_ew(W, 8.0, 7.0, 10.0, H_DOOR, Z_L00_WALL_TOP, label="office door head W")
wall_ew(W, 8.0, 30.0, 33.0, H_DOOR, Z_L00_WALL_TOP, label="office door head E")
wall_ns(P, 20.0, 0.0, 8.0, Z_SUNK, H_L00_CLEAR, T_PART, "office partition")

# Wings at x=8 and x=32. SVG: M 110 110 V 200 M 110 240 V 340 -> 4 m gap y 17..21.
for _x in (8.0, 32.0):
    wall_ns(W, _x, 8.0, 17.0 + KNIT, Z_SUNK, Z_L00_WALL_TOP, label="wing wall N")
    wall_ns(W, _x, 21.0 - KNIT, 31.0, Z_SUNK, Z_L00_WALL_TOP, label="wing wall S")
    wall_ns(W, _x, 17.0, 21.0, H_DOOR, Z_L00_WALL_TOP, label="wing opening head")

# Trade floor south wall at y=31. SVG: M 30 340 H 190 M 270 340 H 430
# -> the 8 m opening into the arrival hall, x 16..24.
wall_ew(W, 31.0, 0.0, 16.0 + KNIT, Z_SUNK, Z_L00_WALL_TOP, label="trade S wall W")
wall_ew(W, 31.0, 24.0 - KNIT, FOOTPRINT, Z_SUNK, Z_L00_WALL_TOP, label="trade S wall E")
# No head over the 8 m opening: H_OPENING is the slab soffit, so the slab is
# the head. A lintel here would put its underside in the soffit plane.

# Cores at x=12 and x=28. SVG: M 150 340 V 380 M 150 410 V 430 -> 3 m door y 35..38.
for _x in (12.0, 28.0):
    wall_ns(W, _x, 31.0, 35.0 + KNIT, Z_SUNK, Z_L00_WALL_TOP, label="core wall N")
    wall_ns(W, _x, 38.0 - KNIT, FOOTPRINT, Z_SUNK, Z_L00_WALL_TOP, label="core wall S")
    wall_ns(W, _x, 35.0, 38.0, H_DOOR, Z_L00_WALL_TOP, label="core door head")

# Service elements sit into the ground slab rather than on its surface.
# F -- faction supply counter, 8.0 x 2.5 m.  SVG rect 70,70 80x25
box(S, 4.0, 12.0, 4.0, 6.5, Z_SUNK, H_COUNTER, "F faction supply")
# R -- registry and insurance, 8.0 x 2.5 m.  SVG rect 310,70 80x25
box(S, 28.0, 36.0, 4.0, 6.5, Z_SUNK, H_COUNTER, "R registry")
# H -- hangar racking, 1.5 m deep run.       SVG rect 35,130 15x190
box(S, T_STRUCT / 2.0, T_STRUCT / 2.0 + 1.5, 10.0, 29.0, Z_SUNK, H_RACK,
    "H racking")
# Q -- quest stand, 3.0 x 2.5 m.             SVG rect 160,350 30x25
box(S, 12.25, 15.25, 32.45, 34.95, Z_SUNK, H_QUEST, "Q quest stand")

# M -- eight market terminals, two banks of four against the wing walls.
MARKET_Y = [(10.0, 13.0), (14.5, 17.5), (19.0, 22.0), (23.5, 26.5)]
MARKET_X = [(8.25, 10.25), (29.75, 31.75)]
market_count = 0
for _mx0, _mx1 in MARKET_X:
    for _my0, _my1 in MARKET_Y:
        box(S, _mx0, _mx1, _my0, _my1, Z_SUNK, H_TERMINAL, "M terminal")
        market_count += 1

# System plate -- a landmark, not a service (A-01 note 3). SVG r=36 and r=24.
cylinder("GB_L00_Plate", 20.0, 19.5, PLATE_R_OUTER, Z_SUNK, 0.05)
cylinder("GB_L00_Plate", 20.0, 19.5, PLATE_R_INNER, Z_SUNK, 0.10)

# Stairs. The SVG hatch runs x 1.5..10.5 (west) and 29.5..38.5 (east),
# y 33..37.5 -- a 9 m run across the core, 4.5 m wide, rising to Level 01.
#
# Only 25 treads are drawn. The 26th is the Level 01 slab itself, so the last
# riser is the step up onto the floor; drawing a 26th tread at 4.5 would put
# its top face in the same plane as the slab it lands on. Each tread reaches
# KNIT into the next, and the whole flight is KNIT wider than the stairwell
# it comes up through, so the slab edge is buried in the stair and the stair's
# sides are buried under the slab.
STAIR_Y = (33.0, 37.5)
STAIR_RISE = Z_L01 / STAIR_STEPS
STAIR_GOING = 9.0 / STAIR_STEPS
STAIR_RUN = STAIR_DRAWN * STAIR_GOING       # where the stairwell opening ends
for _group, _x_start, _dir in (("GB_L00_Stair_West", 1.5, 1.0),
                               ("GB_L00_Stair_East", 38.5, -1.0)):
    for _i in range(STAIR_DRAWN):
        _a = _x_start + _dir * _i * STAIR_GOING
        _b = _x_start + _dir * ((_i + 1) * STAIR_GOING + KNIT)
        box(_group, min(_a, _b), max(_a, _b),
            STAIR_Y[0] - 0.25, STAIR_Y[1] + KNIT,
            Z_SUNK, (_i + 1) * STAIR_RISE, "tread %d" % (_i + 1))

# --------------------------------------------------------------------------
# Level 01 -- career gallery
# --------------------------------------------------------------------------

W1 = "GB_L01_Walls"
F1 = "GB_L01_Fronts"

# The void is the trade floor, x 8..32 by y 8..31 on centrelines. The hole is
# cut KNIT wider than the wall faces around it, so the slab edge ends inside
# the wall instead of flush against it.
VX0, VX1 = 8.25 - KNIT, 31.75 + KNIT
VY0, VY1 = 8.25 - KNIT, 30.75 + KNIT

SLAB_NEG, SLAB_OUT = NEG + KNIT, OUT - KNIT     # buried in the perimeter wall
SW0 = 1.5 + STAIR_RUN                           # west stairwell, east end
SE0 = 38.5 - STAIR_RUN                          # east stairwell, west end

box("GB_L01_Slab", SLAB_NEG, SLAB_OUT, SLAB_NEG, VY0, Z_SLAB_BOT, Z_L01, "slab N")
box("GB_L01_Slab", SLAB_NEG, VX0, VY0, VY1, Z_SLAB_BOT, Z_L01, "slab W")
box("GB_L01_Slab", VX1, SLAB_OUT, VY0, VY1, Z_SLAB_BOT, Z_L01, "slab E")
# South strip, with a stairwell cut over each core stair. The openings stop at
# the top tread rather than at the foot of the flight, so there is floor
# everywhere the stair is not.
box("GB_L01_Slab", SLAB_NEG, SLAB_OUT, VY1, 33.0, Z_SLAB_BOT, Z_L01, "slab S/N")
box("GB_L01_Slab", SLAB_NEG, 1.5, 33.0, 37.5, Z_SLAB_BOT, Z_L01, "slab S/W")
box("GB_L01_Slab", SW0, SE0, 33.0, 37.5, Z_SLAB_BOT, Z_L01, "slab S/landing")
box("GB_L01_Slab", 38.5, SLAB_OUT, 33.0, 37.5, Z_SLAB_BOT, Z_L01, "slab S/E")
box("GB_L01_Slab", SLAB_NEG, SLAB_OUT, 37.5, SLAB_OUT, Z_SLAB_BOT, Z_L01, "slab S/S")

# Bureau band at y=4. SVG: M 490 70 H 610 M 650 70 H 730 M 770 70 H 890
# -> 4 m openings at x 12..16 and x 24..28.
wall_ew(W1, 4.0, 0.0, 12.0 + KNIT, Z_L01_WALL_BOT, Z_ROOF, label="bureau W")
wall_ew(W1, 4.0, 16.0 - KNIT, 24.0 + KNIT, Z_L01_WALL_BOT, Z_ROOF, label="bureau C")
wall_ew(W1, 4.0, 28.0 - KNIT, FOOTPRINT, Z_L01_WALL_BOT, Z_ROOF, label="bureau E")
wall_ew(W1, 4.0, 12.0, 16.0, Z_L01 + H_DOOR, Z_ROOF, label="bureau head W")
wall_ew(W1, 4.0, 24.0, 28.0, Z_L01 + H_DOOR, Z_ROOF, label="bureau head E")

# Six career alcoves, 4 m deep by 5 m, three a side. Numbered C1..C6 on the
# sheet and deliberately unnamed -- career chains are M8 (note 4).
ALCOVE_Y = [(10.0, 15.0), (17.0, 22.0), (24.0, 29.0)]
ALCOVE_SIDES = [(0.0, 4.0, 4.0), (36.0, FOOTPRINT, 36.0)]  # x0, x1, front
alcove_count = 0
for _ax0, _ax1, _front in ALCOVE_SIDES:
    for _ay0, _ay1 in ALCOVE_Y:
        wall_ew(W1, _ay0, _ax0, _ax1, Z_L01_WALL_BOT, Z_ROOF, label="alcove wall")
        wall_ew(W1, _ay1, _ax0, _ax1, Z_L01_WALL_BOT, Z_ROOF, label="alcove wall")
        wall_ns(F1, _front, _ay0, _ay1, Z_L01_WALL_BOT,
                Z_L01 + H_ALCOVE_FRONT, T_PART, "alcove front")
        alcove_count += 1

# Cores and landing. SVG: M 490 348 H 610 M 770 348 H 890, verticals at
# x=12 and x=28 -- so the landing between them is open to the void.
# The sheet draws this wall at y=31.8, which leaves 0.8 m centreline to the
# void edge -- and 0.50 m of air once the wall and the parapet have taken
# their thickness out of it. A 0.68 m pawn cannot get through, and this strip
# is the only route from the landing to the career gallery. Moved south to
# L01_CORE_N, which is as far as the drawn stair at y=33 allows, for 1.30 m
# clear. See report_tight_gaps.
L01_CORE_N = 32.6

wall_ew(W1, L01_CORE_N, 0.0, 12.0 + KNIT, Z_L01_WALL_BOT, Z_ROOF, label="L01 core N/W")
wall_ew(W1, L01_CORE_N, 28.0 - KNIT, FOOTPRINT, Z_L01_WALL_BOT, Z_ROOF, label="L01 core N/E")
for _x in (12.0, 28.0):
    wall_ns(W1, _x, L01_CORE_N, 35.0 + KNIT, Z_L01_WALL_BOT, Z_ROOF, label="L01 core N")
    wall_ns(W1, _x, 38.0 - KNIT, FOOTPRINT, Z_L01_WALL_BOT, Z_ROOF, label="L01 core S")
    wall_ns(W1, _x, 35.0, 38.0, Z_L01 + H_DOOR, Z_ROOF, label="L01 core door head")

# Parapet round the void, foot sunk into the slab. Its inner face oversails
# the slab edge by a nosing rather than sitting flush with it: flush would put
# a Partition face and a Floor face in one plane, which is two different greys
# competing for the same pixels.
R = "GB_L01_Railing"
NOSE = KNIT / 2.0
box(R, VX0 - T_PART, VX1 + T_PART, VY0 - T_PART, VY0 + NOSE, Z_RAIL_BOT,
    Z_L01 + H_RAIL, "parapet N")
box(R, VX0 - T_PART, VX1 + T_PART, VY1 - NOSE, VY1 + T_PART, Z_RAIL_BOT,
    Z_L01 + H_RAIL, "parapet S")
box(R, VX0 - T_PART, VX0 + NOSE, VY0, VY1, Z_RAIL_BOT,
    Z_L01 + H_RAIL, "parapet W")
box(R, VX1 - NOSE, VX1 + T_PART, VY0, VY1, Z_RAIL_BOT,
    Z_L01 + H_RAIL, "parapet E")

# Roof. Its underside dips below the wall tops so it swallows them, and its
# edges stop inside the perimeter rather than flush with its outer face. Not
# an oversail: the envelope stays 40.5 m over the walls, as the sheet has it.
box("GB_Roof", SLAB_NEG, SLAB_OUT, SLAB_NEG, SLAB_OUT,
    Z_ROOF_BOT, Z_ROOF + T_ROOF, "roof")


# --------------------------------------------------------------------------
# Check the model against the sheet's own room schedule
# --------------------------------------------------------------------------

def check_against_sheet():
    """Measure what was built and compare it to the printed schedule.

    Areas are centreline to centreline, which is how the sheet dimensions
    them. A mis-transcribed coordinate shows up here rather than in a
    screenshot three days later.
    """
    measured = {
        "Trade floor":      (32.0 - 8.0) * (31.0 - 8.0),
        "North offices x2": 2 * (20.0 * 8.0),
        "Hangar store":     8.0 * (31.0 - 8.0),
        "Departures":       (40.0 - 32.0) * (31.0 - 8.0),
        "Cores x2":         2 * (12.0 * (40.0 - 31.0)),
        "Arrival hall":     (28.0 - 12.0) * (40.0 - 31.0),
        "Gross per level":  FOOTPRINT * FOOTPRINT,
    }
    printed = {
        "Trade floor": 552, "North offices x2": 320, "Hangar store": 184,
        "Departures": 184, "Cores x2": 216, "Arrival hall": 144,
        "Gross per level": 1600,
    }
    problems = []
    print("")
    print("  room schedule            built      sheet")
    for name, area in measured.items():
        want = printed[name]
        ok = abs(area - want) < 0.51
        print("  %-22s %7.1f m2 %7d m2  %s"
              % (name, area, want, "ok" if ok else "MISMATCH"))
        if not ok:
            problems.append(name)

    counts = [("market terminals", market_count, 8),
              ("career alcoves", alcove_count, 6)]
    for name, got, want in counts:
        ok = got == want
        print("  %-22s %7d    %7d     %s" % (name, got, want, "ok" if ok else "MISMATCH"))
        if not ok:
            problems.append(name)

    if problems:
        raise SystemExit("A-02 transcription disagrees with the sheet: "
                         + ", ".join(problems))


# --------------------------------------------------------------------------
# Build it
# --------------------------------------------------------------------------

def clear_scene():
    for coll in list(bpy.data.collections):
        bpy.data.collections.remove(coll)
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for mesh in list(bpy.data.meshes):
        bpy.data.meshes.remove(mesh)
    for mat in list(bpy.data.materials):
        bpy.data.materials.remove(mat)


def make_materials():
    made = {}
    for name, rgba in MATERIALS.items():
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        mat.diffuse_color = rgba          # Workbench reads this
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = rgba
            if "Roughness" in bsdf.inputs:
                bsdf.inputs["Roughness"].default_value = 0.85
        made[name] = mat
    return made


def build(mats):
    scene = bpy.context.scene
    root = bpy.data.collections.new("A02_CapitalHub")
    scene.collection.children.link(root)

    subs = {}
    for _group, (coll_name, _mat) in GROUPS.items():
        if coll_name not in subs:
            coll = bpy.data.collections.new(coll_name)
            root.children.link(coll)
            subs[coll_name] = coll

    objects = {}
    for group, (coll_name, mat_name) in GROUPS.items():
        data = _geometry[group]
        mesh = bpy.data.meshes.new(group)
        mesh.from_pydata(data["verts"], [], data["faces"])
        mesh.validate()
        mesh.update()
        mesh.materials.append(mats[mat_name])
        obj = bpy.data.objects.new(group, mesh)
        subs[coll_name].objects.link(obj)
        objects[group] = obj

    return root, subs, objects


# --------------------------------------------------------------------------
# Collision
# --------------------------------------------------------------------------
#
# Unreal reads objects named UCX_<mesh>_NN out of the FBX as simple collision,
# one convex hull each, and a walking character sweeps against simple collision
# only: it leaves bTraceComplex false, and the engine treats that as a choice
# of simple or complex rather than as a preference --
# Engine/Private/PhysicsEngine/PhysicsInterfaceUtils.cpp picks exactly one.
#
# A mesh without it is not "roughly" solid, it is intangible, and walking
# through a wall looks precisely like a bug in the movement code. That is what
# happened to the ore deposits, and it cost a playtest to find.
#
# Every solid in this building is an axis-aligned box, and a box is already a
# convex hull, so the collision here is the model rather than an approximation
# of it. No decomposition, nothing to tune, and no way for the two to drift:
# they are generated from the same list of solids in the same run.


# The face winding box() uses, reused so a hull is built the same way a solid
# is. Vertices 0-3 are the bottom ring, 4-7 the top, in the same order.
_HULL_FACES = ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
               (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))


def _hull_verts(x0, x1, y0, y1, z0, z1_west, z1_east):
    """Eight corners in plan coordinates, with the top allowed to slope in x.

    A flat top (z1_west == z1_east) is a box. A sloped one is the wedge the
    stairs get, which is still convex and therefore still one hull.
    """
    bx0, by1 = _plan_to_blender(x0, y0)
    bx1, by0 = _plan_to_blender(x1, y1)

    return [
        (bx0, by0, z0), (bx1, by0, z0), (bx1, by1, z0), (bx0, by1, z0),
        (bx0, by0, z1_west), (bx1, by0, z1_east),
        (bx1, by1, z1_east), (bx0, by1, z1_west),
    ]


# The two stair flights get one ramp each instead of a hull per tread.
#
# <strong>Not a saving -- a decision about how it plays.</strong> Twenty-five
# tread hulls are geometrically honest and unwalkable: the character has no
# step-up, so a sweep into a 17.3 cm riser slides sideways along it and the
# flight becomes a wall. One convex wedge through the tread nosings is a 26.6
# degree incline, which is a slope the walk model already knows what to do with.
#
# The top face passes exactly through every nosing -- height RISE per GOING --
# and is extended one going past the foot so it meets the ground slab flush
# rather than starting with a riser nobody can climb. It lands exactly on the
# Level 01 slab at the far end, which is what makes 26 the right step count
# rather than 25 plus a lip.
def _stair_ramp(group, x_start, direction):
    foot = x_start - direction * STAIR_GOING
    top = x_start + direction * STAIR_RUN

    lo, hi = min(foot, top), max(foot, top)

    # z at the low-x and high-x ends, whichever way this flight climbs.
    z_lo, z_hi = (0.0, Z_L01) if direction > 0 else (Z_L01, 0.0)

    return (group, _hull_verts(lo, hi, STAIR_Y[0] - KNIT, STAIR_Y[1] + KNIT,
                               Z_SUNK, z_lo, z_hi))


STAIR_GROUPS = {"GB_L00_Stair_West": 1.0, "GB_L00_Stair_East": -1.0}


def collision_hulls():
    """One (group, vertices) pair per hull, built from the solids themselves.

    Reads _boxes rather than appending to it: report_coincident_faces measures
    that list, and collision that reached it would report itself as coincident
    with the geometry it was generated from.
    """
    hulls = []

    for group, _label, (x0, x1, y0, y1, z0, z1) in _boxes:
        if group in STAIR_GROUPS:
            continue

        hulls.append((group, _hull_verts(x0, x1, y0, y1, z0, z1, z1)))

    for group, direction in STAIR_GROUPS.items():
        x_start = 1.5 if direction > 0 else 38.5
        hulls.append(_stair_ramp(group, x_start, direction))

    # The system plate is a disc a few centimetres proud of the floor. Nothing
    # can be stopped by it and a character standing on it stands on the floor,
    # so it gets no hull -- and saying that here is cheaper than wondering later
    # why one mesh has none.
    return hulls


def build_collision(root, subs):
    """Attaches a UCX_ object per hull, in a collection no render ever shows."""
    coll = bpy.data.collections.new("Collision")
    root.children.link(coll)

    # Registered in subs so render_previews' only() hides it from every shot:
    # that function hides every collection it is not asked to show, so this
    # cannot be forgotten by adding a view later.
    subs["Collision"] = coll

    counts = {}
    made = []

    for group, verts in collision_hulls():
        counts[group] = counts.get(group, 0) + 1

        # UCX_<mesh name>_NN. The suffix matters: the importer matches the mesh
        # by the name between the prefix and it, so UCX_GB_L00_Walls_01 finds
        # GB_L00_Walls. Import Collision has to be on, and one hull per UCX,
        # or these arrive as twelve extra static meshes instead.
        name = "UCX_%s_%02d" % (group, counts[group])

        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(verts, [], list(_HULL_FACES))
        mesh.validate()
        mesh.update()

        obj = bpy.data.objects.new(name, mesh)
        coll.objects.link(obj)
        made.append(obj)

    print("")
    print("  collision: %d hulls over %d meshes" % (len(made), len(counts)))

    for group in sorted(counts):
        note = " (one ramp, not %d treads)" % STAIR_DRAWN if group in STAIR_GROUPS else ""
        print("    %-22s %3d%s" % (group, counts[group], note))

    return coll, made


def report_bounds(objects):
    lo = [1e9, 1e9, 1e9]
    hi = [-1e9, -1e9, -1e9]
    tris = 0
    for obj in objects.values():
        tris += len(obj.data.polygons)
        for v in obj.data.vertices:
            for i in range(3):
                lo[i] = min(lo[i], v.co[i])
                hi[i] = max(hi[i], v.co[i])
    print("")
    print("  bounds X %.2f .. %.2f  (%.2f m)" % (lo[0], hi[0], hi[0] - lo[0]))
    print("  bounds Y %.2f .. %.2f  (%.2f m)" % (lo[1], hi[1], hi[1] - lo[1]))
    print("  bounds Z %.2f .. %.2f  (%.2f m)" % (lo[2], hi[2], hi[2] - lo[2]))
    print("  objects %d, faces %d" % (len(objects), tris))
    print("  origin is the footprint centre at floor level; nothing is offset.")


# --------------------------------------------------------------------------
# Preview renders
# --------------------------------------------------------------------------

def point_at(obj, target):
    d = Vector(target) - obj.location
    obj.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


def setup_workbench():
    """Flat-shaded solid view. Right for the plans and the cutaway, and
    wrong for interiors -- a ceiling seen from below faces away from the
    view-space studio light and renders black."""
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_cavity = True
    shading.cavity_type = "BOTH"
    shading.show_shadows = True
    shading.show_object_outline = True
    scene.display.render_aa = "16"


def make_world():
    """Strong, even ambient. EEVEE gives no bounce light here, so an
    interior lit only from ceiling panels goes black on every vertical
    surface -- the world is doing the work the bounce would."""
    world = bpy.data.worlds.new("GB_World")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg is not None:
        bg.inputs["Color"].default_value = (0.48, 0.51, 0.54, 1.0)
        bg.inputs["Strength"].default_value = 0.55
    world.color = (0.06, 0.07, 0.08)
    bpy.context.scene.world = world


def make_lights():
    """A rig for the eye-level shots only: modest ceiling panels for shape
    on top of the ambient. Hidden from camera, or they blow out the frame."""
    coll = bpy.data.collections.new("Lights")
    bpy.context.scene.collection.children.link(coll)

    def area(name, plan_x, plan_y, z, size_x, size_y, watts):
        data = bpy.data.lights.new(name, type="AREA")
        data.shape = "RECTANGLE"
        data.size = size_x
        data.size_y = size_y
        data.energy = watts
        obj = bpy.data.objects.new(name, data)
        bx, by = _plan_to_blender(plan_x, plan_y)
        obj.location = (bx, by, z)
        obj.rotation_euler = (math.pi, 0.0, 0.0)      # pointing down
        obj.visible_camera = False
        coll.objects.link(obj)

    # Over the trade floor, just under the 9 m roof.
    area("L_Trade", 20.0, 19.5, 8.6, 18.0, 16.0, 3200.0)
    # Under the Level 01 slab, where the gallery is otherwise a black lid.
    area("L_Arrival", 20.0, 35.0, 4.2, 14.0, 7.0, 700.0)
    area("L_WestWing", 4.0, 19.5, 4.2, 6.0, 20.0, 700.0)
    area("L_EastWing", 36.0, 19.5, 4.2, 6.0, 20.0, 700.0)
    area("L_NorthBand", 20.0, 4.0, 4.2, 30.0, 6.0, 700.0)
    # On Level 01, above the gallery ring.
    area("L_GalleryW", 4.0, 19.5, 8.6, 6.0, 22.0, 700.0)
    area("L_GalleryE", 36.0, 19.5, 8.6, 6.0, 22.0, 700.0)
    area("L_GalleryS", 20.0, 35.0, 8.6, 30.0, 7.0, 700.0)
    area("L_GalleryN", 20.0, 4.0, 8.6, 30.0, 6.0, 700.0)
    return coll


def setup_eevee():
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    # AgX rolls the ceiling panels off instead of clipping them to white.
    try:
        scene.view_settings.view_transform = "AgX"
    except TypeError:
        scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = -0.35
    for attr in ("use_raytracing", "use_shadows"):
        if hasattr(scene.eevee, attr):
            setattr(scene.eevee, attr, True)


def render_to(path, width, height):
    scene = bpy.context.scene
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("  wrote %s" % path)


def render_previews(out_dir, subs, objects):
    scene = bpy.context.scene
    make_world()
    lights = make_lights()

    cam_data = bpy.data.cameras.new("GB_Cam")
    cam = bpy.data.objects.new("GB_Cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam

    def only(*visible):
        for name, coll in subs.items():
            coll.hide_render = name not in visible

    # --- drawings: Workbench, no lights ---------------------------------
    setup_workbench()
    lights.hide_render = True

    # 1. Level 00 plan, looking straight down.
    only("L00_Structure", "L00_Service", "L00_Stairs")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 44.0
    cam.location = (0.0, 0.0, 60.0)
    cam.rotation_euler = (0.0, 0.0, 0.0)
    render_to(os.path.join(out_dir, "A02_plan_L00.png"), 1400, 1400)

    # 2. Level 01 plan. Stairs stay visible so the cores read.
    only("L01_Structure", "L00_Stairs")
    render_to(os.path.join(out_dir, "A02_plan_L01.png"), 1400, 1400)

    # 3. Cutaway from the south-west, roof off.
    only("L00_Structure", "L00_Service", "L00_Stairs", "L01_Structure")
    cam_data.type = "PERSP"
    cam_data.lens = 35.0
    cam.location = (-46.0, -46.0, 40.0)
    point_at(cam, (0.0, -2.0, 3.0))
    render_to(os.path.join(out_dir, "A02_cutaway.png"), 1600, 1000)

    # --- interiors: EEVEE, lit, roof on ---------------------------------
    setup_eevee()
    lights.hide_render = False
    only("L00_Structure", "L00_Service", "L00_Stairs", "L01_Structure", "Shell")
    cam_data.type = "PERSP"

    def shot(name, plan_x, plan_y, z, pitch_deg, lens, w=1600, h=900):
        bx, by = _plan_to_blender(plan_x, plan_y)
        cam.location = (bx, by, z)
        # 90 deg looks level to the north; less than 90 tilts down.
        cam.rotation_euler = (math.radians(pitch_deg), 0.0, 0.0)
        cam_data.lens = lens
        render_to(os.path.join(out_dir, name), w, h)

    # 4. Just inside the 6 m main entrance, looking north across the
    #    arrival hall and through the 8 m opening -- note 2's shot.
    shot("A02_entrance.png", 20.0, 38.5, 1.7, 89.0, 20.0)

    # 5. On the trade floor by the system plate, looking up at the gallery.
    #    Tests the figcaption's claim that the career givers read as above
    #    the trade the game is about.
    shot("A02_tradefloor.png", 20.0, 26.0, 1.7, 103.0, 20.0)

    # 6. At the parapet on the Level 01 landing, looking down into the void.
    #    Standing back from the rail does not work -- at 6.2 m eye height a
    #    1.1 m parapet 2.5 m away hides the entire trade floor.
    shot("A02_gallery.png", 20.0, 31.6, 6.2, 66.0, 24.0)

    for coll in subs.values():
        coll.hide_render = False
    bpy.data.objects.remove(cam, do_unlink=True)
    for obj in list(lights.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.collections.remove(lights)


# --------------------------------------------------------------------------
# Export
# --------------------------------------------------------------------------

def export_fbx(path):
    try:
        bpy.ops.preferences.addon_enable(module="io_scene_fbx")
    except Exception:
        pass
    try:
        bpy.ops.export_scene.fbx(
            filepath=path,
            use_selection=False,
            apply_unit_scale=True,
            apply_scale_options="FBX_SCALE_UNITS",
            object_types={"MESH"},
            mesh_smooth_type="FACE",
            use_mesh_modifiers=True,
            bake_space_transform=False,
            axis_forward="-Z",
            axis_up="Y",
        )
        print("  wrote %s" % path)
        return True
    except Exception as exc:                      # noqa: BLE001
        print("  FBX export failed: %s" % exc)
        return False


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out_dir = os.getcwd()
    if "--out" in argv:
        out_dir = argv[argv.index("--out") + 1]
    out_dir = os.path.abspath(out_dir)
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    # The .blend and the renders are working files and are kept outside the
    # game repository; only the FBX is committed, because only the FBX is what
    # the engine consumes. Two destinations rather than one, so the split is
    # enforced here instead of remembered at the call site.
    fbx_path = os.path.join(out_dir, "A02_CapitalHub.fbx")
    if "--fbx" in argv:
        fbx_path = os.path.abspath(argv[argv.index("--fbx") + 1])
        if not os.path.isdir(os.path.dirname(fbx_path)):
            os.makedirs(os.path.dirname(fbx_path))

    print("")
    print("A-02 Capital Hub greybox")
    print("  blend and renders: %s" % out_dir)
    print("  fbx:               %s" % fbx_path)

    check_names_are_distinct()
    check_against_sheet()
    coincident = report_coincident_faces()
    blocked = report_tight_gaps(0.0, 'Level 00')
    blocked += report_tight_gaps(Z_L01, 'Level 01')

    if "--check-only" in argv:
        print("")
        raise SystemExit(1 if (coincident or blocked) else 0)

    clear_scene()
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"

    mats = make_materials()
    root, subs, objects = build(mats)
    report_bounds(objects)

    # Before the export, and not part of `objects`: bounds and face counts
    # describe the thing that is drawn, and collision is not drawn.
    build_collision(root, subs)

    print("")
    export_fbx(fbx_path)
    render_previews(out_dir, subs, objects)

    # Roof hidden on open, so the file shows an interior when you load it.
    objects["GB_Roof"].hide_viewport = True

    blend = os.path.join(out_dir, "A02_CapitalHub.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    print("  wrote %s" % blend)
    print("")


main()
