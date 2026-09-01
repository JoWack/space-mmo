#!/usr/bin/env python
"""
Greybox for sheet A-01 "Trading Hub" of the Origin Station Plans.

    blender --background --python tools/greybox/a01_trading_hub.py -- \
        --out <working dir> --fbx <engine file>

Scale, derived and then proved three times. The sheet's 25 m dimension spans
SVG x=50..300, which is 250 units, so 10 units = 1 m. Three further numbers
fall out of that exactly, and none of them was used to derive it:

  * the 6 m workshop depth dimension  (SVG y 30..90)
  * the airlock at 6.0 x 3.5 m, which is the size the parts schedule gives A
  * the concourse at 454 m2 and the workshop at 150 m2, both as scheduled

Plan coordinates: x 0->25 west to east, y 0->25 north to south (SVG y runs
southward), z 0 at the floor. Origin ends up at the footprint centre, floor at
Z=0, and nothing here records a world position.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from greybox_lib import (                                          # noqa: E402
    box, build, build_collision, check_schedule, cylinder, configure,
    export_fbx, make_lights, make_world, parse_args, point_at, render_to,
    report_bounds, report_coincident_faces, report_tight_gaps, set_extent,
    setup_eevee, setup_workbench, wall_ew, wall_ns, clear_scene)

import math                                                        # noqa: E402
import bpy                                                         # noqa: E402

# --------------------------------------------------------------------------
# Off the sheet
# --------------------------------------------------------------------------

FOOTPRINT = 25.0            # title block: 25 x 25 m
T_STRUCT = 0.5              # conventions: structural wall, 0.5 m nominal
T_PART = 0.2                # conventions: partition, non-structural

H_CONCOURSE = 4.0           # note 4: "Ceiling 4 m in the concourse"
H_WORKSHOP = 5.0            # note 4: "5 m over the workshop"
T_SLAB = 0.5
H_DOOR = 3.0                # note 4: "Doors 3 m, main entrance 3 m" -- widths.
                            # Head heights are not on the sheet; this is the
                            # assumption, same as A-02.

H_BAY = 2.6                 # J industry bay, a machine you stand at
H_TERMINAL = 1.2            # M market terminal
H_QUEST = 1.2               # Q quest stand
H_RACK = 2.5                # H hangar racking
PLATE_R_OUTER, PLATE_R_INNER = 2.6, 1.7     # note 3: "a 5 m disc"

KNIT = 0.1
Z_SUNK = -KNIT

# ASpaceMMOCharacterPawn: CollisionRadiusCentimetres = 34,
# CharacterHeightCentimetres = 180.
PAWN_DIAMETER, PAWN_HEIGHT, MIN_WALKWAY = 0.68, 1.80, 1.20

MATERIALS = {
    "MAT_GB_Structure": (0.52, 0.54, 0.55, 1.0),
    "MAT_GB_Partition": (0.71, 0.73, 0.74, 1.0),
    "MAT_GB_Floor":     (0.38, 0.40, 0.41, 1.0),
    "MAT_GB_Service":   (0.09, 0.39, 0.37, 1.0),
    "MAT_GB_Roof":      (0.30, 0.32, 0.33, 1.0),
}

GROUPS = {
    "GB_A01_Floor":      ("Structure", "MAT_GB_Floor"),
    "GB_A01_Walls":      ("Structure", "MAT_GB_Structure"),
    "GB_A01_Partitions": ("Structure", "MAT_GB_Partition"),
    "GB_A01_Service":    ("Service",   "MAT_GB_Service"),
    "GB_A01_Plate":      ("Service",   "MAT_GB_Partition"),
    "GB_A01_Roof":       ("Shell",     "MAT_GB_Roof"),
}

configure(GROUPS, MATERIALS)
set_extent(FOOTPRINT, FOOTPRINT)

W, P, S = "GB_A01_Walls", "GB_A01_Partitions", "GB_A01_Service"
OUT, NEG = FOOTPRINT + T_STRUCT / 2.0, -T_STRUCT / 2.0
SLAB_NEG, SLAB_OUT = NEG + KNIT, OUT - KNIT

# --------------------------------------------------------------------------
# Shell.  SVG: M 50 30 H 300 V 280 H 190 M 160 280 H 50 V 30
# -> the only opening in the perimeter is the 3 m south entrance, x 11..14.
# --------------------------------------------------------------------------

box("GB_A01_Floor", SLAB_NEG, SLAB_OUT, SLAB_NEG, SLAB_OUT, -T_SLAB, 0.0,
    "ground slab")

# North and south run the full width and own the corners; east and west butt
# into them, so no two perimeter faces share an outer plane.
wall_ew(W, 0.0, NEG, OUT, Z_SUNK, H_WORKSHOP, T_STRUCT, "perimeter N")
wall_ew(W, FOOTPRINT, NEG, 11.0 + KNIT, Z_SUNK, H_CONCOURSE, T_STRUCT,
        "perimeter S/W")
wall_ew(W, FOOTPRINT, 14.0 - KNIT, OUT, Z_SUNK, H_CONCOURSE, T_STRUCT,
        "perimeter S/E")
wall_ew(W, FOOTPRINT, 11.0, 14.0, H_DOOR, H_CONCOURSE, T_STRUCT,
        "main entrance head")

for _x, _label in ((0.0, "perimeter W"), (FOOTPRINT, "perimeter E")):
    wall_ns(W, _x, T_STRUCT / 2.0, 6.0, Z_SUNK, H_WORKSHOP, T_STRUCT,
            _label + "/workshop")
    wall_ns(W, _x, 6.0, FOOTPRINT - T_STRUCT / 2.0, Z_SUNK, H_CONCOURSE,
            T_STRUCT, _label + "/concourse")

# Workshop partition at y=6, 3 m door at x 11..14. Note 1: this is the one
# room that comes out if industry moves to a spaceport, so it is a partition
# across the shell rather than part of it.
wall_ew(W, 6.0, 0.0, 11.0 + KNIT, Z_SUNK, H_WORKSHOP, T_STRUCT, "workshop wall W")
wall_ew(W, 6.0, 14.0 - KNIT, FOOTPRINT, Z_SUNK, H_WORKSHOP, T_STRUCT,
        "workshop wall E")
wall_ew(W, 6.0, 11.0, 14.0, H_DOOR, H_WORKSHOP, T_STRUCT, "workshop door head")

# Airlock. SVG: M 145 245 V 280 M 205 245 V 280 M 145 245 H 160 M 190 245 H 205
# -> a 6.0 x 3.5 m vestibule, exactly the A of the parts schedule.
wall_ns(P, 9.5, 21.5, FOOTPRINT, Z_SUNK, H_CONCOURSE, T_PART, "airlock W")
wall_ns(P, 15.5, 21.5, FOOTPRINT, Z_SUNK, H_CONCOURSE, T_PART, "airlock E")
wall_ew(P, 21.5, 9.5, 11.0 + KNIT, Z_SUNK, H_CONCOURSE, T_PART, "airlock N/W")
wall_ew(P, 21.5, 14.0 - KNIT, 15.5, Z_SUNK, H_CONCOURSE, T_PART, "airlock N/E")
wall_ew(P, 21.5, 11.0, 14.0, H_DOOR, H_CONCOURSE, T_PART, "airlock N head")

# --------------------------------------------------------------------------
# Fitted out.  Furniture seats flush to its wall -- a plan's rectangles sit a
# quarter metre off, which leaves a slot no character can enter.
# --------------------------------------------------------------------------

# J x3, the three kinds of work. SVG rects 62,42 / 145,42 / 218,42.
box(S, T_STRUCT / 2.0, T_STRUCT / 2.0 + 7.0, T_STRUCT / 2.0,
    T_STRUCT / 2.0 + 3.0, Z_SUNK, H_BAY, "J refinery")
box(S, 9.5, 15.5, T_STRUCT / 2.0, T_STRUCT / 2.0 + 3.0, Z_SUNK,
    H_BAY, "J fabricator")
box(S, FOOTPRINT - T_STRUCT / 2.0 - 7.0, FOOTPRINT - T_STRUCT / 2.0,
    T_STRUCT / 2.0, T_STRUCT / 2.0 + 3.0, Z_SUNK, H_BAY, "J assembly")

# H, the hangar store: a long run against the west wall plus a return.
box(S, T_STRUCT / 2.0, T_STRUCT / 2.0 + 1.5, 7.5, 19.5, Z_SUNK, H_RACK,
    "H racking run")
box(S, T_STRUCT / 2.0 + 1.5, T_STRUCT / 2.0 + 3.3, 12.5, 15.5, Z_SUNK, H_RACK,
    "H racking return")

# M x3 against the east wall. SVG rects 275,110 / 275,155 / 275,200.
MARKET_Y = [(8.0, 11.0), (12.5, 15.5), (17.0, 20.0)]
market_count = 0
for _y0, _y1 in MARKET_Y:
    box(S, FOOTPRINT - T_STRUCT / 2.0 - 2.0, FOOTPRINT - T_STRUCT / 2.0,
        _y0, _y1, Z_SUNK, H_TERMINAL, "M terminal")
    market_count += 1

# Q, beside the airlock so it is the first thing past the door. SVG rect
# 215,248 -- shifted west to sit flush on the vestibule rather than 0.25 m off.
box(S, 15.5 + T_PART / 2.0, 15.5 + T_PART / 2.0 + 3.0,
    FOOTPRINT - T_STRUCT / 2.0 - 2.5, FOOTPRINT - T_STRUCT / 2.0,
    Z_SUNK, H_QUEST, "Q quest stand")

# The system plate: a landmark, not a service (note 3). SVG r=26 and r=17.
cylinder("GB_A01_Plate", 12.2, 14.5, PLATE_R_OUTER, Z_SUNK, 0.05)
cylinder("GB_A01_Plate", 12.2, 14.5, PLATE_R_INNER, Z_SUNK, 0.10)

# Roofs, stepped because the workshop is a metre taller. Undersides dip below
# the wall tops so they swallow them.
box("GB_A01_Roof", SLAB_NEG, SLAB_OUT, SLAB_NEG, 6.0 + KNIT,
    H_WORKSHOP - KNIT, H_WORKSHOP + T_SLAB, "roof workshop")
box("GB_A01_Roof", SLAB_NEG, SLAB_OUT, 6.0 - KNIT, SLAB_OUT,
    H_CONCOURSE - KNIT, H_CONCOURSE + T_SLAB, "roof concourse")


# --------------------------------------------------------------------------

def checks():
    airlock = (15.5 - 9.5) * (FOOTPRINT - 21.5)
    check_schedule(
        measured={
            "Concourse m2": (FOOTPRINT * (FOOTPRINT - 6.0)) - airlock,
            "Workshop m2": FOOTPRINT * 6.0,
            "Airlock m2": airlock,
            "Gross m2": FOOTPRINT * FOOTPRINT,
            "Market terminals": market_count,
            "Industry bays": 3,
        },
        published={
            "Concourse m2": 454, "Workshop m2": 150, "Airlock m2": 21,
            "Gross m2": 625, "Market terminals": 3, "Industry bays": 3,
        })
    bad = report_coincident_faces()
    bad += report_tight_gaps(
        0.0, "Concourse and workshop", PAWN_DIAMETER, PAWN_HEIGHT,
        MIN_WALKWAY,
        ("MAT_GB_Structure", "MAT_GB_Partition", "MAT_GB_Service"))
    return bad


def previews(out_dir, subs):
    make_world()
    lights = make_lights([
        ("L_Concourse", 12.5, 15.0, H_CONCOURSE - 0.3, 16.0, 16.0, 2600.0),
        ("L_Workshop", 12.5, 3.0, H_WORKSHOP - 0.3, 20.0, 5.0, 1400.0),
    ])
    cam_data = bpy.data.cameras.new("GB_Cam")
    cam = bpy.data.objects.new("GB_Cam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam

    def only(*names):
        for name, coll in subs.items():
            coll.hide_render = name not in names

    setup_workbench()
    lights.hide_render = True
    only("Structure", "Service")
    cam_data.type, cam_data.ortho_scale = "ORTHO", 28.0
    cam.location, cam.rotation_euler = (0.0, 0.0, 40.0), (0.0, 0.0, 0.0)
    render_to(os.path.join(out_dir, "A01_plan.png"), 1400, 1400)

    cam_data.type, cam_data.lens = "PERSP", 35.0
    cam.location = (-30.0, -30.0, 26.0)
    point_at(cam, (0.0, -1.0, 2.0))
    render_to(os.path.join(out_dir, "A01_cutaway.png"), 1600, 1000)

    setup_eevee()
    lights.hide_render = False
    only("Structure", "Service", "Shell")
    cam_data.lens = 22.0
    bx, by = 12.5 - FOOTPRINT / 2.0, FOOTPRINT / 2.0 - 22.5
    cam.location, cam.rotation_euler = (bx, by, 1.7), (math.radians(89.0), 0, 0)
    render_to(os.path.join(out_dir, "A01_entrance.png"), 1600, 900)

    for coll in subs.values():
        coll.hide_render = False
    bpy.data.objects.remove(cam, do_unlink=True)
    for obj in list(lights.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.collections.remove(lights)


def main():
    out_dir, fbx_path, check_only = parse_args()
    print("")
    print("A-01 Trading Hub greybox")
    failures = checks()
    if check_only:
        raise SystemExit(1 if failures else 0)

    # Clears Blender's own data only. The solids live in the library's Python
    # state, which is what build() and the checks both read.
    clear_scene()
    root, subs, objects = build("A01_TradingHub")
    report_bounds(objects)
    build_collision(root, subs)
    print("")
    export_fbx(fbx_path or os.path.join(out_dir, "A01_TradingHub.fbx"))
    previews(out_dir, subs)
    objects["GB_A01_Roof"].hide_viewport = True
    blend = os.path.join(out_dir, "A01_TradingHub.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    print("  wrote %s" % blend)
    print("")


main()
