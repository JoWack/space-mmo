#!/usr/bin/env python3
"""
Borlash City (sheet A-07 of the Origin Station Plans), rebuilt round from the concept art. Task 167.

    blender --factory-startup --background --python-exit-code 1 \
        --python tools/greybox/a07_borlash_city.py -- [--check-only] [--render] [--out DIR] [--engine DIR]

or exec'd inside a running Blender (the MCP session does this), where it builds into the open file.

SOURCES, AND WHICH ONE WINS
  * Layout figure: Joe's sketch (Sketches/BorlashCitySketch.png) and the CapitalGrandDistrict concept
    art agree on a ROUND walled city: four gates on the cardinals, four docking stations outside
    the diagonal corners, four district quadrants, the town square and fountain at the crossing.
    Plan set A-07 squared the wall off; Joe asked for the city "as close to ... the following images"
    as possible (2 October 2026), so the wall is round again. Task 154 kept the square and said so.
  * Dimensions: plan set A-07 wherever it states one -- 400 m wall to wall, 584 m across the docks,
    16 m gate avenues, a 12 m ring road, 12 m walls, the 17-row station schedule's footprints.
  * Silhouettes, materials and colour: the concept sheets (domes with brass ribs, gothic HQ spires,
    blue glass, warm stone, the city standing in water reached by bridges).
  * The city is ONE station (station_capital_hub), decided by Joe on 2 October: one hangar, one book,
    one industry queue; the four docks are its only berths; each building's counters open only that
    building's service. Nothing here encodes that -- the manifest lists berths and service points and
    the engine side decides what they do.

CONVENTIONS
  * Blender metres, +X east, +Y north, +Z up. The origin is the levelled ground at the city centre;
    the walking surface is FLOOR above it, so the terrain is never coplanar with the paving.
  * The city is flat and the planet is not: across 400 m of a 20 km world the ground falls away by
    d^2/2R, 4 m at the docks. The terrain is levelled under the city by a pad (task 168); the city's
    platform carries a skirt deep enough to stay buried where the levelled sphere drops below it.
  * Solids overlap rather than meet (KNIT). Inlaid surfaces sit at distinct heights per layer.
  * Collision is generated from the same calls as the geometry. Only what a person or a ship can
    reach gets hulls: the platform, walls at street level, furniture, railings, trunks and posts.
    Roofs, domes and spires have none -- characters cannot climb and ships may not fly here.
  * The character is 0.68 m wide, 1.80 m tall, jumps 0.90 m (JumpSpeed 420 cm/s at 9.81 m/s^2) and
    has no step-up, so every walkable surface is one level or a ramp, and 1.1 m railings seal water.
"""

import json
import math
import os
import sys
import time
from collections import deque

import bpy
from mathutils import Vector

# --------------------------------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------------------------------

_HERE = os.path.dirname(os.path.abspath(globals().get("__file__", "tools/greybox/a07_borlash_city.py")))
ROOT = os.path.dirname(os.path.dirname(_HERE))
DEFAULT_OUT = "D:/Documents/SpaceMMOAssets/Blender/Stations/BorlashCity"
DEFAULT_ENGINE = os.path.join(ROOT, "client", "RawContent", "Stations", "A07_BorlashCity")

# --------------------------------------------------------------------------------------------------
# The numbers. Each says where it came from.
# --------------------------------------------------------------------------------------------------

FLOOR = 0.30          # walking surface above the levelled ground at the centre; keeps terrain off the paving
KNIT = 0.10           # how far a solid reaches into whatever it meets
R_DRAWN = 20000.0     # the drawn planet radius, metres (task 123: every body is drawn at 20 km)

# Character (ASpaceMMOCharacterPawn, FCharacterWalkModel)
PAWN_D, PAWN_H, JUMP_H = 0.68, 1.80, 0.90
ROUTE_MIN = 1.20      # plan set constraint 3: no route narrower than 1.2 m clear
RAIL_H = 1.10         # above the 0.90 m jump, so water behind a railing cannot be reached

# Platform and lagoon. A-07: "584 m across the docks"; docks at the corners of the road square.
PLAT = 272.0          # half-width of the square platform: outer face of the perimeter road
ROAD_IN = 262.0       # perimeter road inner edge (10 m road)
DOCK_C = 260.0        # dock centres at (+-260, +-260), as A-07 places them
DOCK_R = 32.0         # 260 + 32 = 292, so 584 m across the docks
PAD_OFF, PAD_R = 14.0, 12.0     # landing pad: 14 m outboard of the dock centre, 24 m across
HUB_OFF, HUB_R = 10.0, 8.0      # docking hub rotunda: 10 m inboard
WATER_Z = FLOOR - 0.70          # lagoon surface
# The canal's own surface, 0.20 m under the paving. It shared the lagoon's 0.70 until 2 October, when the
# arithmetic caught up with it: the ground is a sphere, and 36 m out it is only 3 cm below the plane, so
# the terrain stood 36 cm above the canal's water and covered it. check_water_over_ground measures it.
CANAL_WATER_Z = FLOOR - 0.20
BED_Z = -1.40                   # lagoon bed
SKIRT_Z = -6.50                 # platform bottom; the levelled sphere is 4.0 m below the plane at 400 m
RAMP_LEN = 24.0                 # countryside ramps at the four axes, down to the levelled ground

# Wall. A-07: "400 m wall to wall", "walls reach 12 m", "gate towers 20 m".
WALL_RI, WALL_RO, WALL_TOP = 196.0, 204.0, 12.0
WALL_SEGMENTS = 128
GATE_HALF, GATE_HEAD = 8.2, 9.0          # 16 m avenue through a 16.4 m opening
POSTERN_HALF, POSTERN_HEAD = 5.2, 7.0    # corner posterns for the dock causeways
GATE_TOWER_R, GATE_TOWER_OFF = 7.0, 16.0
TURRET_R = 4.2
BRIDGE_HALF = 9.5                        # gate bridge deck; its railings start inside the gate towers
CAUSEWAY_HALF = 6.2                      # causeway deck; its railings start inside the postern towers
RAIL_OFF = 0.25                          # a railing's centreline sits this far inside the edge it guards
ROAD_TOP = FLOOR - 0.006                 # roads, bridges and causeways: just under the paving they run into

# Inside. A-07: ring road 12 m, gate avenues 16 m, town square 48 x 48 (made round: r 35).
RING_RI = 184.0
AVENUE_HALF = 8.0
CANAL_RI, CANAL_RO = 36.0, 42.0          # the canal ringing the square, from the concept's centre
PLAZA_R = CANAL_RI
DISTRICT_LIMIT = 176.0                   # every building corner inside this, 8 m clear of the ring road

# Buildings. Footprints are A-07's station schedule; positions are A-07's, moved inward where the
# round wall cuts a corner the square one did not (each move is noted on its row).
H_HALL = FLOOR + 7.0                     # ground-floor hall ceiling, 7 m clear
WALL_T = 0.8

# --------------------------------------------------------------------------------------------------
# Materials. One look per name; the engine gets the same numbers through the manifest.
# --------------------------------------------------------------------------------------------------

MATERIALS = {
    # Colours are written as sRGB, the way a designer reads them off the concept sheet, and converted
    # to linear once in srgb(); Blender and Unreal both take linear values. Written linear by mistake
    # on the first pass, the warm tan stone rendered nearly white and the brass did not read as gold.
    # name:        (base colour sRGB,          metallic, roughness, emission colour sRGB, strength)
    "Stone":       ((0.75, 0.63, 0.45),        0.00, 0.72, None, 0.0),
    "StoneDark":   ((0.33, 0.31, 0.29),        0.00, 0.85, None, 0.0),
    "Brass":       ((0.86, 0.66, 0.33),        1.00, 0.32, None, 0.0),
    "Slate":       ((0.20, 0.24, 0.29),        0.35, 0.50, None, 0.0),
    "GlassDome":   ((0.24, 0.42, 0.60),        0.60, 0.15, (0.20, 0.50, 0.85), 0.8),
    "Glass":       ((0.45, 0.75, 1.00),        0.00, 0.18, (0.45, 0.80, 1.00), 3.5),
    "Light":       ((0.75, 0.93, 1.00),        0.00, 0.30, (0.70, 0.92, 1.00), 9.0),
    "LightWarm":   ((1.00, 0.80, 0.55),        0.00, 0.30, (1.00, 0.76, 0.45), 7.0),
    "Water":       ((0.09, 0.25, 0.34),        0.00, 0.04, None, 0.0),
    "Grass":       ((0.30, 0.45, 0.22),        0.00, 0.90, None, 0.0),
    "Foliage":     ((0.18, 0.34, 0.17),        0.00, 0.85, None, 0.0),
    "Bark":        ((0.33, 0.25, 0.18),        0.00, 0.90, None, 0.0),
    "Paving":      ((0.55, 0.53, 0.48),        0.00, 0.80, None, 0.0),
    "PavingLight": ((0.70, 0.66, 0.58),        0.00, 0.74, None, 0.0),
    "Road":        ((0.36, 0.36, 0.37),        0.00, 0.85, None, 0.0),
    "Metal":       ((0.22, 0.23, 0.26),        0.85, 0.40, None, 0.0),
    "Pad":         ((0.13, 0.14, 0.16),        0.30, 0.60, None, 0.0),
    "Interior":    ((0.74, 0.69, 0.61),        0.00, 0.80, None, 0.0),
    "Floor":       ((0.42, 0.35, 0.29),        0.10, 0.42, None, 0.0),
    "LagoonBed":   ((0.14, 0.17, 0.16),        0.00, 0.95, None, 0.0),
}


def srgb(c):
    """sRGB channel(s) to linear."""
    def one(v):
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return tuple(one(v) for v in c)
MAT_PREFIX = "MAT_Borlash_"
MESH_PREFIX = "SM_Borlash_"

# Surface layers: every inlaid surface has its own height, so no two of them can ever share a plane.
LAYER = {
    "Grass": 0.015, "Road": 0.020, "Floor": 0.020, "Pad": 0.022, "Path": 0.025, "Spoke": 0.025,
    "RingEdge": 0.027, "Avenue": 0.030, "PlazaRing": 0.030, "Inlay": 0.035, "PadRing": 0.035,
}

# --------------------------------------------------------------------------------------------------
# Geometry store
# --------------------------------------------------------------------------------------------------


class MeshData:
    __slots__ = ("name", "coll", "v", "f", "fm", "fs", "fsolid", "hulls")

    def __init__(self, name, coll):
        self.name, self.coll = name, coll
        self.v, self.f, self.fm, self.fs, self.fsolid, self.hulls = [], [], [], [], [], []


MESHES = {}
SOLIDS = []          # (bbox, planes) of every convex solid, for "is this patch sealed inside another"
BUILDINGS = []       # schedule records, for the checks and the manifest
SERVICE_POINTS = []
BERTHS = []
DOORS = []
GATES = []
TARGETS = []         # (name, x, y) the route check must reach
NEGATIVES = []       # (name, x, y) it must not
FREE_ZONES = []      # rectangles/circles other features must keep clear of: (kind, data, label)
SCENERY = []         # closed buildings filling the districts: dicts with rect, height, roof
WATER_SURFACES = []  # (name, nearest distance to the centre in m, surface height in m) of visible water


def reset():
    for store in (MESHES,):
        store.clear()
    for lst in (SOLIDS, BUILDINGS, SERVICE_POINTS, BERTHS, DOORS, GATES, TARGETS, NEGATIVES, FREE_ZONES,
                SCENERY, WATER_SURFACES):
        del lst[:]


def M(key, coll):
    name = MESH_PREFIX + key
    if name not in MESHES:
        MESHES[name] = MeshData(name, coll)
    return MESHES[name]


# ---- small vector helpers -------------------------------------------------------------------------

def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    length = math.sqrt(dot(a, a))
    return (a[0] / length, a[1] / length, a[2] / length) if length > 1e-12 else (0.0, 0.0, 0.0)


def centroid(points):
    n = float(len(points))
    return (sum(p[0] for p in points) / n, sum(p[1] for p in points) / n, sum(p[2] for p in points) / n)


def newell(poly):
    nx = ny = nz = 0.0
    for i in range(len(poly)):
        x0, y0, z0 = poly[i]
        x1, y1, z1 = poly[(i + 1) % len(poly)]
        nx += (y0 - y1) * (z0 + z1)
        ny += (z0 - z1) * (x0 + x1)
        nz += (x0 - x1) * (y0 + y1)
    return (nx, ny, nz)


def polar(r, a, cx=0.0, cy=0.0):
    return (cx + r * math.cos(a), cy + r * math.sin(a))


def ngon(cx, cy, r, n, rot=0.0):
    return [(cx + r * math.cos(rot + 2.0 * math.pi * i / n), cy + r * math.sin(rot + 2.0 * math.pi * i / n))
            for i in range(n)]


def at_z(ring, z):
    return [(x, y, z) for x, y in ring]


# ---- the one solid builder ------------------------------------------------------------------------

def loft(m, ring0, ring1, mat, top=None, bottom=None, side=None, smooth=False, collide=False):
    """A convex solid between two planar rings; ring1 may be a single apex point.

    Winding is fixed here against the axis from ring0 to ring1, so callers never think about it.
    `side` may be a function of the face's outward normal, which is how a wall gets stone outside and
    plaster inside. Caps carry their own vertices so smooth sides never average with them.
    `collide` is False, True (an obstacle) or "floor" (somewhere to stand).
    """
    apex = len(ring1) == 1
    if dot(newell(ring0), sub(centroid(ring1), centroid(ring0))) < 0.0:
        ring0 = list(reversed(ring0))
        if not apex:
            ring1 = list(reversed(ring1))
    n = len(ring0)
    base = len(m.v)
    first_face = len(m.f)
    solid_id = len(SOLIDS)
    m.v.extend(ring0)
    m.v.extend(ring1)
    faces = []
    for i in range(n):
        j = (i + 1) % n
        if apex:
            faces.append(((base + i, base + j, base + n), "side"))
        else:
            faces.append(((base + i, base + j, base + n + j, base + n + i), "side"))
    b2 = len(m.v)
    m.v.extend(ring0)
    faces.append((tuple(b2 + i for i in reversed(range(n))), "bottom"))
    if not apex:
        b3 = len(m.v)
        m.v.extend(ring1)
        faces.append((tuple(b3 + i for i in range(n)), "top"))

    centre = centroid(list(ring0) + list(ring1))
    planes = []
    for idx, role in faces:
        poly = [m.v[k] for k in idx]
        nrm = newell(poly)
        area2 = math.sqrt(dot(nrm, nrm))
        if area2 < 1e-12:
            raise SystemExit("degenerate face in %s" % m.name)
        unit = mul(nrm, 1.0 / area2)
        fc = centroid(poly)
        if dot(unit, sub(fc, centre)) <= -1e-9:
            raise SystemExit("inward face in %s near %s" % (m.name, str(fc)))
        planes.append((unit, dot(unit, fc)))
        if role == "side":
            fmat = side(unit) if callable(side) else (side or mat)
            fsm = smooth
        elif role == "top":
            fmat, fsm = top or mat, False
        else:
            fmat, fsm = bottom or mat, False
        m.f.append(idx)
        m.fm.append(fmat)
        m.fs.append(fsm)
        m.fsolid.append(solid_id)
    pts = list(ring0) + list(ring1)
    bbox = (min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts),
            max(p[1] for p in pts), min(p[2] for p in pts), max(p[2] for p in pts))
    SOLIDS.append((bbox, planes))
    if collide:
        m.hulls.append({"verts": list(ring0) + list(ring1), "faces": _hull_faces(len(ring0), apex),
                        "kind": "floor" if collide == "floor" else "block"})
    return first_face


def _hull_faces(n, apex):
    faces = []
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, j, n) if apex else (i, j, n + j, n + i))
    faces.append(tuple(reversed(range(n))))
    if not apex:
        faces.append(tuple(n + i for i in range(n)))
    return faces


def prism(m, ring, z0, z1, mat, **kw):
    return loft(m, at_z(ring, z0), at_z(ring, z1), mat, **kw)


def aabox(m, x0, x1, y0, y1, z0, z1, mat, **kw):
    return prism(m, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1, mat, **kw)


def obox(m, cx, cy, w, d, z0, z1, ang, mat, **kw):
    c, s = math.cos(ang), math.sin(ang)
    ring = [(cx + c * a - s * b, cy + s * a + c * b)
            for a, b in ((-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2), (-w / 2, d / 2))]
    return prism(m, ring, z0, z1, mat, **kw)


def seg(m, a, b, width, z0, z1, mat, extend=0.0, **kw):
    """A box from plan point a to plan point b, `width` across."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    return obox(m, (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, length + 2 * extend, width, z0, z1,
                math.atan2(dy, dx), mat, **kw)


def frustum(m, cx, cy, r0, r1, z0, z1, n, mat, rot=None, **kw):
    rot = math.pi / n if rot is None else rot
    ring0 = at_z(ngon(cx, cy, r0, n, rot), z0)
    ring1 = [(cx, cy, z1)] if r1 <= 1e-6 else at_z(ngon(cx, cy, r1, n, rot), z1)
    return loft(m, ring0, ring1, mat, **kw)


def sector(m, ri, ro, a0, a1, z0, z1, mat, cx=0.0, cy=0.0, **kw):
    ring = [polar(ri, a0, cx, cy), polar(ro, a0, cx, cy), polar(ro, a1, cx, cy), polar(ri, a1, cx, cy)]
    return prism(m, ring, z0, z1, mat, **kw)


def annulus(m, ri, ro, z0, z1, mat, n=64, cx=0.0, cy=0.0, skip=None, **kw):
    """A ring of convex sectors; `skip(a_mid)` drops a sector (an opening)."""
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        if skip is not None and skip((a0 + a1) / 2):
            continue
        sector(m, ri, ro, a0, a1, z0, z1, mat, cx, cy, **kw)


def beam(m, p0, p1, w, t, out_hint, mat, **kw):
    """An oriented box from 3D point p0 to p1: `w` across, `t` along out_hint."""
    a = norm(sub(p1, p0))
    s = norm(cross(a, out_hint))
    if s == (0.0, 0.0, 0.0):
        s = norm(cross(a, (0.0, 0.0, 1.0)))
    tt = norm(cross(a, s))
    if dot(tt, out_hint) < 0:
        tt = mul(tt, -1.0)
    corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    r0 = [add(p0, add(mul(s, cs * w / 2), mul(tt, ct * t / 2))) for cs, ct in corners]
    r1 = [add(p1, add(mul(s, cs * w / 2), mul(tt, ct * t / 2))) for cs, ct in corners]
    return loft(m, r0, r1, mat, **kw)


def sphere(m, cx, cy, cz, r, mat, nseg=10, nring=6, sz=1.0):
    """A convex UV sphere, smooth, recorded as one solid."""
    base = len(m.v)
    solid_id = len(SOLIDS)
    m.v.append((cx, cy, cz - r * sz))
    for k in range(1, nring):
        phi = -math.pi / 2 + math.pi * k / nring
        rr, zz = r * math.cos(phi), cz + r * sz * math.sin(phi)
        for i in range(nseg):
            a = 2 * math.pi * i / nseg
            m.v.append((cx + rr * math.cos(a), cy + rr * math.sin(a), zz))
    m.v.append((cx, cy, cz + r * sz))
    top = len(m.v) - 1

    def ring_idx(k, i):
        return base + 1 + (k - 1) * nseg + (i % nseg)

    faces = []
    for i in range(nseg):
        faces.append((base, ring_idx(1, i + 1), ring_idx(1, i)))
    for k in range(1, nring - 1):
        for i in range(nseg):
            faces.append((ring_idx(k, i), ring_idx(k, i + 1), ring_idx(k + 1, i + 1), ring_idx(k + 1, i)))
    for i in range(nseg):
        faces.append((ring_idx(nring - 1, i), ring_idx(nring - 1, i + 1), top))
    planes = []
    centre = (cx, cy, cz)
    for idx in faces:
        poly = [m.v[k] for k in idx]
        nrm = norm(newell(poly))
        fc = centroid(poly)
        if dot(nrm, sub(fc, centre)) <= 0:
            raise SystemExit("inward sphere face in %s" % m.name)
        planes.append((nrm, dot(nrm, fc)))
        m.f.append(idx)
        m.fm.append(mat)
        m.fs.append(True)
        m.fsolid.append(solid_id)
    SOLIDS.append(((cx - r, cx + r, cy - r, cy + r, cz - r * sz, cz + r * sz), planes))


def dome(m, cx, cy, r, z0, h, mat, n=24, bands=8, profile="round", ribs=0, rib_mat="Brass",
         rib_w=0.30, rib_t=0.30):
    """A smooth dome on a flat base, with optional brass ribs down its meridians.

    round: a hemisphere stretched to height h. pointed: an ogive, the gothic profile. onion: bulges
    past its base before closing, which is not convex, so it is never recorded as a sealing solid.
    """
    def profile_at(t):
        if profile == "round":
            a = t * math.pi / 2
            return r * math.cos(a), z0 + h * math.sin(a)
        if profile == "pointed":
            return r * (1.0 - t ** 1.7) ** 0.75, z0 + h * t
        # onion
        return r * (1.0 + 0.22 * math.sin(math.pi * min(1.0, t * 1.35))) * (1.0 - t) ** 0.85, z0 + h * t

    rings = [profile_at(k / float(bands)) for k in range(bands)]
    base = len(m.v)
    solid_id = len(SOLIDS)
    for rr, zz in rings:
        for i in range(n):
            a = 2 * math.pi * i / n + math.pi / n
            m.v.append((cx + rr * math.cos(a), cy + rr * math.sin(a), zz))
    apex = len(m.v)
    m.v.append((cx, cy, z0 + h))
    faces = []
    for k in range(bands - 1):
        for i in range(n):
            j = (i + 1) % n
            faces.append((base + k * n + i, base + k * n + j, base + (k + 1) * n + j, base + (k + 1) * n + i))
    for i in range(n):
        j = (i + 1) % n
        faces.append((base + (bands - 1) * n + i, base + (bands - 1) * n + j, apex))
    for idx in faces:
        m.f.append(idx)
        m.fm.append(mat)
        m.fs.append(True)
        m.fsolid.append(solid_id)
    cap = len(m.v)
    for i in range(n):
        a = 2 * math.pi * i / n + math.pi / n
        m.v.append((cx + r * math.cos(a), cy + r * math.sin(a), z0))
    m.f.append(tuple(cap + i for i in reversed(range(n))))
    m.fm.append(mat)
    m.fs.append(False)
    m.fsolid.append(solid_id)
    if profile != "onion":
        planes = []
        allf = faces + [tuple(cap + i for i in reversed(range(n)))]
        cen = (cx, cy, z0 + h * 0.35)
        for idx in allf:
            poly = [m.v[k] for k in idx]
            nrm = norm(newell(poly))
            planes.append((nrm, dot(nrm, centroid(poly))))
        SOLIDS.append(((cx - r, cx + r, cy - r, cy + r, z0, z0 + h), planes))
    else:
        SOLIDS.append(((cx, cx, cy, cy, z0, z0), []))   # placeholder id, seals nothing
    if ribs:
        for i in range(ribs):
            a = 2 * math.pi * i / ribs
            ca, sa = math.cos(a), math.sin(a)
            steps = bands * 2
            pts = []
            for k in range(steps + 1):
                t = min(k / float(steps), 0.97)
                rr, zz = profile_at(t)
                pts.append((cx + (rr + rib_t * 0.35) * ca, cy + (rr + rib_t * 0.35) * sa, zz))
            for k in range(steps):
                beam(m, pts[k], pts[k + 1], rib_w, rib_t, (ca, sa, 0.25), rib_mat)


def lantern(m, cx, cy, z0, r, h, spire_h, n=12):
    """A small glazed drum, a cap and a brass needle: the crown of every dome in the concept."""
    frustum(m, cx, cy, r, r, z0 - KNIT, z0 + h, n, "Stone")
    for i in range(n // 2):
        a = 2 * math.pi * (i + 0.5) / (n // 2)
        p = polar(r - 0.05, a, cx, cy)
        obox(m, p[0], p[1], 0.18, r * 0.55, z0 + h * 0.2, z0 + h * 0.8, a, "Glass")
    frustum(m, cx, cy, r * 1.15, r * 1.15, z0 + h - 0.05, z0 + h + 0.25, n, "Brass")
    dome(m, cx, cy, r * 1.05, z0 + h + 0.2, r * 0.9, "Slate", n=n, bands=5)
    frustum(m, cx, cy, r * 0.32, 0.0, z0 + h + 0.2 + r * 0.8, z0 + h + 0.2 + r * 0.8 + spire_h, 8, "Brass")
    sphere(m, cx, cy, z0 + h + 0.2 + r * 0.8 + spire_h + 0.25, 0.32, "Light", 8, 5)


def drum_dome(m, cx, cy, r, z0, drum_h, dome_h, dome_mat="GlassDome", n=24, ribs=12, windows=12,
              profile="round", lantern_r=None, spire_h=5.0):
    frustum(m, cx, cy, r, r, z0 - KNIT, z0 + drum_h, n, "Stone", smooth=True)
    for i in range(windows):
        a = 2 * math.pi * (i + 0.5) / windows
        p = polar(r - 0.06, a, cx, cy)
        width = min(1.6, 2 * math.pi * r / windows * 0.45)
        obox(m, p[0], p[1], 0.20, width, z0 + drum_h * 0.18, z0 + drum_h * 0.78, a, "Glass")
    frustum(m, cx, cy, r + 0.35, r + 0.35, z0 + drum_h - 0.1, z0 + drum_h + 0.45, n, "Brass", smooth=True)
    dome(m, cx, cy, r, z0 + drum_h + 0.35, dome_h, dome_mat, n=n, bands=9, profile=profile, ribs=ribs)
    if lantern_r:
        lantern(m, cx, cy, z0 + drum_h + 0.35 + dome_h * 0.94, lantern_r, lantern_r * 1.4, spire_h)
    return z0 + drum_h + 0.35 + dome_h


def pinnacle(m, cx, cy, s, z0, z1, spire, mat="Stone"):
    aabox(m, cx - s / 2, cx + s / 2, cy - s / 2, cy + s / 2, z0, z1, mat)
    aabox(m, cx - s / 2 - 0.12, cx + s / 2 + 0.12, cy - s / 2 - 0.12, cy + s / 2 + 0.12, z1 - 0.05, z1 + 0.25,
          "Brass")
    frustum(m, cx, cy, s * 0.62, 0.0, z1 + 0.2, z1 + 0.2 + spire, 4, "Slate", rot=math.pi / 4)
    sphere(m, cx, cy, z1 + 0.35 + spire, max(0.15, s * 0.16), "Brass", 6, 4)


def round_tower(m, cx, cy, r, z0, z1, cap_h, cap="cone", n=16, slits=4, band=True, finial=True,
                collide=False, light_mat="Glass"):
    frustum(m, cx, cy, r, r, z0, z1, n, "Stone", smooth=True, collide=collide)
    if band:
        frustum(m, cx, cy, r + 0.18, r + 0.18, z1 - 1.6, z1 - 1.1, n, "Brass", smooth=True)
    frustum(m, cx, cy, r + 0.35, r + 0.35, z1 - 0.1, z1 + 0.9, n, "StoneDark", smooth=True)
    for i in range(slits):
        a = 2 * math.pi * (i + 0.5) / slits
        p = polar(r - 0.05, a, cx, cy)
        obox(m, p[0], p[1], 0.22, 0.5, z0 + (z1 - z0) * 0.45, z0 + (z1 - z0) * 0.72, a, light_mat)
    if cap == "cone":
        frustum(m, cx, cy, r + 0.25, 0.0, z1 + 0.85, z1 + 0.85 + cap_h, n, "Slate", smooth=False)
        top = z1 + 0.85 + cap_h
    else:
        dome(m, cx, cy, r + 0.1, z1 + 0.85, cap_h, "Slate", n=n, bands=6, profile=cap,
             ribs=max(4, n // 4))
        top = z1 + 0.85 + cap_h
    if finial:
        frustum(m, cx, cy, 0.22, 0.0, top - 0.3, top + 1.6, 6, "Brass")
        sphere(m, cx, cy, top + 0.4, 0.26, "Brass", 6, 4)
    return top


# --------------------------------------------------------------------------------------------------
# Facades: a rectangle's side as a frame, and things placed on it
# --------------------------------------------------------------------------------------------------

def side_frame(rect, side):
    """(start point, tangent, outward normal, length) of one side of an axis-aligned rectangle."""
    x0, x1, y0, y1 = rect
    if side == "S":
        return (x0, y0), (1.0, 0.0), (0.0, -1.0), x1 - x0
    if side == "N":
        return (x1, y1), (-1.0, 0.0), (0.0, 1.0), x1 - x0
    if side == "E":
        return (x1, y0), (0.0, 1.0), (1.0, 0.0), y1 - y0
    return (x0, y1), (0.0, -1.0), (-1.0, 0.0), y1 - y0


def on_face(rect, side, s, w, out0, out1, z0, z1, mat, **kw):
    """A box on a facade: centred s along it, w wide, from out0 to out1 proud of the face."""
    (sx, sy), (tx, ty), (nx, ny), _l = side_frame(rect, side)
    a0, a1 = s - w / 2, s + w / 2
    ring = [(sx + tx * a + nx * o, sy + ty * a + ny * o) for a, o in ((a0, out0), (a1, out0), (a1, out1),
                                                                      (a0, out1))]
    return prism(m_current[0], ring, z0, z1, mat, **kw)


def lancet(rect, side, s, w, z0, z1, mat="Glass", frame=True):
    """A pointed window: glass panel, a pointed head, a brass frame behind and a sill."""
    m = m_current[0]
    (sx, sy), (tx, ty), (nx, ny), _l = side_frame(rect, side)
    on_face(rect, side, s, w, -0.05, 0.07, z0, z1, mat)
    head = w * 0.55

    def pt(a, o, z):
        return (sx + tx * a + nx * o, sy + ty * a + ny * o, z)

    tri = [pt(s - w / 2, -0.05, z1 - 0.02), pt(s + w / 2, -0.05, z1 - 0.02), pt(s, -0.05, z1 + head)]
    loft(m, tri, [add(p, (nx * 0.12, ny * 0.12, 0.0)) for p in tri], mat)
    if frame:
        f = 0.16
        on_face(rect, side, s, w + 2 * f, -0.05, 0.035, z0 - f, z1, "Brass")
        tri2 = [pt(s - w / 2 - f, -0.05, z1 - 0.03), pt(s + w / 2 + f, -0.05, z1 - 0.03),
                pt(s, -0.05, z1 + head + f * 1.6)]
        loft(m, tri2, [add(p, (nx * 0.085, ny * 0.085, 0.0)) for p in tri2], "Brass")
        on_face(rect, side, s, w + 0.5, -0.05, 0.22, z0 - f - 0.22, z0 - f, "StoneDark")


m_current = [None]


# --------------------------------------------------------------------------------------------------
# The ground: platform, lagoon, island, roads, causeways, docks' bases, ramps
# --------------------------------------------------------------------------------------------------

def gate_axes():
    """(name, axis angle) of the four gates; north is +Y."""
    return [("North", math.pi / 2), ("East", 0.0), ("South", -math.pi / 2), ("West", math.pi)]


def postern_axes():
    return [("NE", math.pi / 4), ("NW", 3 * math.pi / 4), ("SW", -3 * math.pi / 4), ("SE", -math.pi / 4)]


def ang_dist(a, b):
    d = (a - b + math.pi) % (2 * math.pi) - math.pi
    return abs(d)


def build_ground():
    m = M("Ground", "Site")
    w = M("Water", "Site")
    # The collision floor is one plain square at the walking height, plus the dock disks and ramps:
    # it carries the lagoon too, which the railings then make unreachable. A floor that matched the
    # water would let a fallen player stand a metre down with no step-up to climb out.
    m.hulls.append(_hull_box(-PLAT, PLAT, -PLAT, PLAT, SKIRT_Z, FLOOR, "floor"))

    # Lagoon bed and the platform's outer skirt.
    aabox(m, -PLAT, PLAT, -PLAT, PLAT, SKIRT_Z, BED_Z, "StoneDark", top="LagoonBed")
    # Water: one sheet; where it passes under the island and the roads it is inside them and unseen.
    w_ring = [(-PLAT + 0.2, -PLAT + 0.2), (PLAT - 0.2, -PLAT + 0.2), (PLAT - 0.2, PLAT - 0.2),
              (-PLAT + 0.2, PLAT - 0.2)]
    _sheet(w, w_ring, WATER_Z, "Water")
    # Seen only beyond the wall: inside it the island covers it. Nearest the centre at the wall's foot.
    WATER_SURFACES.append(("lagoon", WALL_RO, WATER_Z))

    # The island inside the wall: the square's paving and the district ground, split by the canal.
    frustum(m, 0, 0, PLAZA_R, PLAZA_R, -2.0, FLOOR, 64, "StoneDark", top="Paving")
    annulus(m, CANAL_RO, WALL_RO - KNIT, -2.0, FLOOR, "StoneDark", n=128, top="Paving")
    # Canal bed, and the canal's own water: higher than the lagoon's, because the ground under it is (see
    # CANAL_WATER_Z). Each edge runs into the wall it meets.
    annulus(m, CANAL_RI - KNIT, CANAL_RO + KNIT, -2.2, -1.35, "LagoonBed", n=64)
    _ring_sheet(w, CANAL_RI - KNIT, CANAL_RO + KNIT, CANAL_WATER_Z, 64)
    WATER_SURFACES.append(("canal", CANAL_RI, CANAL_WATER_Z))

    # Perimeter roads: the square of roads joining the docks, from the sketch.
    for sx, sy, horizontal in ((0, 1, True), (0, -1, True), (1, 0, False), (-1, 0, False)):
        if horizontal:
            y0, y1 = (ROAD_IN, PLAT) if sy > 0 else (-PLAT, -ROAD_IN)
            aabox(m, -DOCK_C, DOCK_C, y0, y1, -1.5, ROAD_TOP, "StoneDark", top="Road")
        else:
            x0, x1 = (ROAD_IN, PLAT) if sx > 0 else (-PLAT, -ROAD_IN)
            aabox(m, x0, x1, -DOCK_C, DOCK_C, -1.5, ROAD_TOP, "StoneDark", top="Road")

    # Gate bridges: each gate avenue continues out across the lagoon to the perimeter road. Wider than
    # the avenue so the railings can start inside the gate towers and leave no gap to the water.
    for name, a in gate_axes():
        p0, p1 = polar(WALL_RI + 2.0, a), polar(ROAD_IN + 1.0, a)
        seg(m, p0, p1, 2 * BRIDGE_HALF, -1.5, ROAD_TOP, "StoneDark", top="Road")
        # Piers along the deck's flanks, for the look of a bridge rather than a dam.
        for k in range(1, 5):
            c = polar(WALL_RO + (ROAD_IN - WALL_RO) * k / 5.0, a)
            for side in (-1, 1):
                t = (-math.sin(a) * side * (BRIDGE_HALF + 0.1), math.cos(a) * side * (BRIDGE_HALF + 0.1))
                obox(m, c[0] + t[0], c[1] + t[1], 3.2, 0.4, WATER_Z - 0.05, FLOOR - 0.12, a, "Metal")

    # Causeways from the corner posterns to the docks.
    for name, a in postern_axes():
        p0 = polar(WALL_RI + 2.0, a)
        p1 = polar(DOCK_C * math.sqrt(2) - DOCK_R + 2.0, a)
        seg(m, p0, p1, 2 * CAUSEWAY_HALF, -1.5, ROAD_TOP, "StoneDark", top="Road")

    # Countryside ramps at the four axes, from the perimeter road down to the levelled ground.
    for name, a in gate_axes():
        ramp(m, a)


def _hull_box(x0, x1, y0, y1, z0, z1, kind):
    verts = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    return {"verts": verts, "faces": _hull_faces(4, False), "kind": kind}


def _sheet(m, ring, z, mat):
    """A one-sided horizontal surface facing up (water)."""
    base = len(m.v)
    m.v.extend(at_z(ring, z))
    poly = [m.v[base + i] for i in range(len(ring))]
    idx = tuple(base + i for i in range(len(ring)))
    if newell(poly)[2] < 0:
        idx = tuple(reversed(idx))
    m.f.append(idx)
    m.fm.append(mat)
    m.fs.append(False)
    m.fsolid.append(-1)


def _ring_sheet(m, ri, ro, z, n, cx=0.0, cy=0.0):
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        _sheet(m, [polar(ri, a0, cx, cy), polar(ro, a0, cx, cy), polar(ro, a1, cx, cy), polar(ri, a1, cx, cy)],
               z, "Water")


def curvature_drop(d):
    return d * d / (2.0 * R_DRAWN)


def ramp(m, a):
    """A wedge from the road's outer edge down to below the levelled ground at its foot.

    Ends half a metre under the ground so the character stands on the terrain where the two meet:
    standing is the higher of the two answers, so a ramp end that stopped above the ground would be
    a ledge with no step-up to climb it.
    """
    d0, d1 = PLAT - 0.6, PLAT + RAMP_LEN
    z_foot = -curvature_drop(d1) - 0.5
    c, s = math.cos(a), math.sin(a)
    t = (-s, c)
    hw = AVENUE_HALF

    def p(d, lat, z):
        return (c * d + t[0] * lat, s * d + t[1] * lat, z)

    bottom = [p(d0, -hw, SKIRT_Z + 2.0), p(d1, -hw, SKIRT_Z + 2.0), p(d1, hw, SKIRT_Z + 2.0),
              p(d0, hw, SKIRT_Z + 2.0)]
    top = [p(d0, -hw, FLOOR), p(d1, -hw, z_foot), p(d1, hw, z_foot), p(d0, hw, FLOOR)]
    loft(m, bottom, top, "StoneDark", top="Road", collide="floor")
    for k in range(4):
        z = FLOOR + (z_foot - FLOOR) * (k + 0.5) / 4.0
        for lat in (-hw - 0.05, hw + 0.05):
            q = p(d0 + (d1 - d0) * (k + 0.5) / 4.0, lat, z)
            obox(m, q[0], q[1], (d1 - d0) / 4.0 - 0.4, 0.25, z - 1.0, z + 0.05, a, "Brass")
    TARGETS.append(("ramp foot %s" % _axis_name(a), c * (d1 - 2.0), s * (d1 - 2.0)))


def _axis_name(a):
    for name, ax in gate_axes():
        if ang_dist(a, ax) < 0.01:
            return name
    return "%.0f" % math.degrees(a)


# --------------------------------------------------------------------------------------------------
# Railings: the only thing between a walker and the water
# --------------------------------------------------------------------------------------------------

def rail_seg(m, a, b, thick=0.45, collide=True):
    """A balustrade from a to b: stone body, brass cap. One hull per straight run."""
    if math.hypot(b[0] - a[0], b[1] - a[1]) < 0.3:
        return
    seg(m, a, b, thick, FLOOR - KNIT, FLOOR + RAIL_H - 0.12, "StoneDark", collide=collide)
    seg(m, a, b, thick + 0.14, FLOOR + RAIL_H - 0.14, FLOOR + RAIL_H, "Brass", extend=0.07)


def rail_arc(m, r, a0, a1, cx=0.0, cy=0.0, step_deg=6.0):
    n = max(1, int(math.ceil(abs(math.degrees(a1 - a0)) / step_deg)))
    for i in range(n):
        p0 = polar(r, a0 + (a1 - a0) * i / n, cx, cy)
        p1 = polar(r, a0 + (a1 - a0) * (i + 1) / n, cx, cy)
        # Extended a little so neighbouring straight runs overlap at the joint rather than leaving a
        # wedge-shaped gap on the outside of the bend.
        dx, dy = p1[0] - p0[0], p1[1] - p0[1]
        L = math.hypot(dx, dy)
        e = 0.12 / max(L, 1e-6)
        rail_seg(m, (p0[0] - dx * e, p0[1] - dy * e), (p1[0] + dx * e, p1[1] + dy * e))


def arc_gaps(gaps):
    """Angular intervals of a full circle left after removing `gaps` (list of (centre, half))."""
    spans = []
    edges = []
    for c, h in gaps:
        c = (c + math.pi) % (2 * math.pi) - math.pi
        edges.append((c - h, c + h))
    edges.sort()
    # Walk the circle from the end of the last gap to the start of the first.
    for i, (lo, hi) in enumerate(edges):
        nlo = edges[(i + 1) % len(edges)][0]
        if i == len(edges) - 1:
            nlo += 2 * math.pi
        if nlo - hi > 1e-6:
            spans.append((hi, nlo))
    return spans


DOCK_RAIL_R = DOCK_R - 0.3      # the dock's own railing circle
JOIN = 0.9                      # how far one railing runs past another it meets, so the fence is closed


def _x_on_dock_rail(offset):
    """Along a perimeter road, where a railing `offset` from the dock centre line meets the dock rail."""
    return DOCK_C - math.sqrt(DOCK_RAIL_R ** 2 - offset ** 2)


def build_railings():
    """Every edge between somewhere walkable and the water.

    A gap at a junction is a way into the lagoon, so every run ends inside the thing it meets: bridge
    railings start inside the gate towers, causeway railings inside the postern towers, road railings
    run on past the dock's railing and the dock's railing runs on past theirs. The route check's
    negative controls are what prove this, not this comment.
    """
    m = M("Railings", "Props")
    off = RAIL_OFF
    # Canal: both banks, open at the four avenue bridges and four diagonal footbridges.
    inner_gaps, outer_gaps = [], []
    for _n, a in gate_axes():
        inner_gaps.append((a, math.asin((AVENUE_HALF + 0.3 - JOIN) / (CANAL_RI - off))))
        outer_gaps.append((a, math.asin((AVENUE_HALF + 0.3 - JOIN) / (CANAL_RO + off))))
    for _n, a in postern_axes():
        inner_gaps.append((a, math.asin((3.3 - JOIN) / (CANAL_RI - off))))
        outer_gaps.append((a, math.asin((3.3 - JOIN) / (CANAL_RO + off))))
    for lo, hi in arc_gaps(inner_gaps):
        rail_arc(m, CANAL_RI - off, lo, hi)
    for lo, hi in arc_gaps(outer_gaps):
        rail_arc(m, CANAL_RO + off, lo, hi)
    # Bridge sides across the canal, running past both bank railings.
    for _n, a in gate_axes():
        _bridge_sides(m, a, AVENUE_HALF + off, CANAL_RI - off - JOIN, CANAL_RO + off + JOIN)
    for _n, a in postern_axes():
        _bridge_sides(m, a, 3.0 + off, CANAL_RI - off - JOIN, CANAL_RO + off + JOIN)

    # Gate bridges across the lagoon: from inside the gate towers to past the road's railing.
    for _n, a in gate_axes():
        _bridge_sides(m, a, BRIDGE_HALF - off, WALL_RI + 4.0, ROAD_IN + off + JOIN)
    # Causeways: from inside the postern towers to past the dock's railing.
    lat = CAUSEWAY_HALF - off
    dock_meet = DOCK_C * math.sqrt(2) - math.sqrt(DOCK_RAIL_R ** 2 - lat ** 2)
    for _n, a in postern_axes():
        _bridge_sides(m, a, lat, WALL_RO + 0.5, dock_meet + JOIN)

    # Perimeter roads: lagoon side and countryside side, each running past the dock's railing.
    lagoon_y, country_y = ROAD_IN + off, PLAT - off
    x_in = _x_on_dock_rail(lagoon_y - DOCK_C) + JOIN
    x_out = _x_on_dock_rail(country_y - DOCK_C) + JOIN
    for sgn in (1, -1):
        for horizontal in (True, False):
            for y, x_end, gap in ((lagoon_y, x_in, BRIDGE_HALF - off - JOIN), (country_y, x_out, AVENUE_HALF)):
                for x_lo, x_hi in ((-x_end, -gap), (gap, x_end)):
                    a, b = (x_lo, sgn * y), (x_hi, sgn * y)
                    if not horizontal:
                        a, b = (a[1], a[0]), (b[1], b[0])
                    rail_seg(m, a, b)


def _bridge_sides(m, a, lateral, d0, d1):
    c, s = math.cos(a), math.sin(a)
    t = (-s, c)
    for side in (-1, 1):
        p0 = (c * d0 + t[0] * side * lateral, s * d0 + t[1] * side * lateral)
        p1 = (c * d1 + t[0] * side * lateral, s * d1 + t[1] * side * lateral)
        rail_seg(m, p0, p1)


# --------------------------------------------------------------------------------------------------
# The wall, its turrets, gates and posterns
# --------------------------------------------------------------------------------------------------

def in_opening(a, r):
    """Is the point at angle a, radius r inside a gate or postern opening?"""
    for _n, g in gate_axes():
        if ang_dist(a, g) < math.pi / 2 and abs(r * math.sin(a - g)) < GATE_HALF:
            return True
    for _n, p in postern_axes():
        if ang_dist(a, p) < math.pi / 2 and abs(r * math.sin(a - p)) < POSTERN_HALF:
            return True
    return False


def build_wall():
    m = M("Wall", "Wall")
    m_current[0] = m
    da = 2 * math.pi / WALL_SEGMENTS
    openings = [(g, math.asin(GATE_HALF / WALL_RI), "gate") for _n, g in gate_axes()]
    openings += [(p, math.asin(POSTERN_HALF / WALL_RI), "postern") for _n, p in postern_axes()]
    for i in range(WALL_SEGMENTS):
        a0, a1 = i * da, (i + 1) * da
        pieces = [(a0, a1)]
        for c, h, _k in openings:
            nxt = []
            for lo, hi in pieces:
                g_lo, g_hi = _unwrap_near(c - h, lo), _unwrap_near(c + h, lo)
                if g_hi <= lo or g_lo >= hi:
                    nxt.append((lo, hi))
                    continue
                if g_lo > lo:
                    nxt.append((lo, g_lo))
                if g_hi < hi:
                    nxt.append((g_hi, hi))
            pieces = nxt
        for lo, hi in pieces:
            if hi - lo < 1e-4:
                continue
            sector(m, WALL_RI, WALL_RO, lo, hi, -1.5, WALL_TOP, "Stone", collide=True)
            # Courses and trim on the outer face: a dark string course, a brass band, light slits. Where
            # a piece ends at an opening the trim stops 8 cm short of the jamb rather than in its plane.
            tlo = lo + (0.0004 if abs(lo - a0) > 1e-9 else 0.0)
            thi = hi - (0.0004 if abs(hi - a1) > 1e-9 else 0.0)
            sector(m, WALL_RO - 0.05, WALL_RO + 0.16, tlo, thi, 3.4, 3.8, "StoneDark")
            sector(m, WALL_RO - 0.05, WALL_RO + 0.12, tlo, thi, 9.6, 10.15, "Brass")
            sector(m, WALL_RI - 0.12, WALL_RI + 0.05, tlo, thi, 9.6, 10.0, "StoneDark")
            # Parapets and merlons. The walk on top is not reachable yet, so none of this collides.
            sector(m, WALL_RO - 0.9, WALL_RO + 0.1, lo, hi, WALL_TOP - 0.1, WALL_TOP + 1.25, "Stone")
            sector(m, WALL_RI - 0.1, WALL_RI + 0.8, lo, hi, WALL_TOP - 0.1, WALL_TOP + 1.0, "Stone")
            mid, half = (lo + hi) / 2, (hi - lo) / 2
            if hi - lo > 0.012:
                sector(m, WALL_RO - 0.85, WALL_RO + 0.05, mid - half * 0.45, mid + half * 0.45,
                       WALL_TOP + 1.15, WALL_TOP + 2.15, "Stone")
            sector(m, WALL_RO - 0.95, WALL_RO + 0.15, lo, hi, WALL_TOP + 1.2, WALL_TOP + 1.32, "Brass")
            if i % 3 == 1 and hi - lo > 0.015:
                p = polar(WALL_RO - 0.02, mid)
                obox(m, p[0], p[1], 0.22, 0.55, 4.6, 8.4, mid, "Glass")
    # Lintels over the openings, continuous with the wall above the head.
    for c, h, kind in openings:
        head = GATE_HEAD if kind == "gate" else POSTERN_HEAD
        if kind == "postern":
            sector(m, WALL_RI, WALL_RO, c - h - 0.003, c + h + 0.003, head, WALL_TOP, "Stone")
            sector(m, WALL_RO - 0.9, WALL_RO + 0.1, c - h, c + h, WALL_TOP - 0.1, WALL_TOP + 1.25, "Stone")
    build_turrets(m)
    for name, a in gate_axes():
        build_gate(m, name, a)
    for name, a in postern_axes():
        build_postern(m, name, a)


def _unwrap_near(a, ref):
    while a < ref - math.pi:
        a += 2 * math.pi
    while a > ref + math.pi:
        a -= 2 * math.pi
    return a


def build_turrets(m):
    for k in range(32):
        a = 2 * math.pi * k / 32
        if any(ang_dist(a, g) < math.radians(8.5) for _n, g in gate_axes()):
            continue
        if any(ang_dist(a, p) < math.radians(5.0) for _n, p in postern_axes()):
            continue
        cx, cy = polar(WALL_RO + 1.4, a)
        round_tower(m, cx, cy, TURRET_R, -1.5, 15.5, 4.8, cap="cone", n=16, slits=3, collide=True)


def build_gate(m, name, a):
    """Two domed towers and a gatehouse over a 16 m opening. A-07: gate towers 20 m."""
    c, s = math.cos(a), math.sin(a)
    t = (-s, c)
    for side in (-1, 1):
        cx = c * (WALL_RI + 5.0) + t[0] * side * GATE_TOWER_OFF
        cy = s * (WALL_RI + 5.0) + t[1] * side * GATE_TOWER_OFF
        frustum(m, cx, cy, GATE_TOWER_R, GATE_TOWER_R, -1.5, 22.0, 20, "Stone", smooth=True, collide=True)
        frustum(m, cx, cy, GATE_TOWER_R + 0.6, GATE_TOWER_R + 0.6, -1.5, 1.6, 20, "StoneDark", smooth=True,
                collide=True)
        frustum(m, cx, cy, GATE_TOWER_R + 0.2, GATE_TOWER_R + 0.2, 13.0, 13.5, 20, "Brass", smooth=True)
        frustum(m, cx, cy, GATE_TOWER_R + 0.5, GATE_TOWER_R + 0.5, 21.9, 23.1, 20, "StoneDark", smooth=True)
        # Windows on every face of the tower, three storeys.
        for level, (z0, z1) in enumerate(((4.5, 8.0), (10.0, 12.6), (15.0, 19.5))):
            for k in range(8):
                wa = 2 * math.pi * (k + 0.5) / 8
                p = polar(GATE_TOWER_R - 0.05, wa, cx, cy)
                obox(m, p[0], p[1], 0.22, 0.9, z0, z1, wa, "Glass")
        dome(m, cx, cy, GATE_TOWER_R + 0.1, 23.0, 6.5, "GlassDome", n=20, bands=8, ribs=10)
        lantern(m, cx, cy, 29.0, 1.3, 1.9, 4.0)
    # Gatehouse spanning the opening, its front face carrying a pointed brass arch.
    lo, hi = -GATE_TOWER_OFF + GATE_TOWER_R * 0.6, GATE_TOWER_OFF - GATE_TOWER_R * 0.6
    ring = []
    for lat, d in ((lo, WALL_RI - 1.0), (hi, WALL_RI - 1.0), (hi, WALL_RO + 1.2), (lo, WALL_RO + 1.2)):
        ring.append((c * d + t[0] * lat, s * d + t[1] * lat))
    prism(m, ring, GATE_HEAD, 18.0, "Stone")
    ring2 = [(c * d + t[0] * lat, s * d + t[1] * lat)
             for lat, d in ((lo - 0.2, WALL_RI - 1.2), (hi + 0.2, WALL_RI - 1.2), (hi + 0.2, WALL_RO + 1.4),
                            (lo - 0.2, WALL_RO + 1.4))]
    prism(m, ring2, 17.9, 18.6, "Brass")
    for lat in (-5.0, 0.0, 5.0):
        q = (c * (WALL_RO + 1.25) + t[0] * lat, s * (WALL_RO + 1.25) + t[1] * lat)
        obox(m, q[0], q[1], 1.1, 0.24, 12.0, 16.5, a + math.pi / 2, "Glass")
    for sgn_d, out in ((WALL_RO + 1.2, 1.0), (WALL_RI - 1.0, -1.0)):
        # Pointed arch frame on both faces of the gatehouse and the jambs below it.
        for side in (-1, 1):
            j = (c * (sgn_d + out * 0.15) + t[0] * side * (GATE_HALF + 0.35),
                 s * (sgn_d + out * 0.15) + t[1] * side * (GATE_HALF + 0.35))
            obox(m, j[0], j[1], 0.7, 0.5, -0.2, GATE_HEAD, a + math.pi / 2, "Brass")
        foot_l = (c * (sgn_d + out * 0.15) - t[0] * GATE_HALF, s * (sgn_d + out * 0.15) - t[1] * GATE_HALF)
        foot_r = (c * (sgn_d + out * 0.15) + t[0] * GATE_HALF, s * (sgn_d + out * 0.15) + t[1] * GATE_HALF)
        apex = (c * (sgn_d + out * 0.15), s * (sgn_d + out * 0.15))
        tymp = [(foot_l[0], foot_l[1], GATE_HEAD + 0.05), (foot_r[0], foot_r[1], GATE_HEAD + 0.05),
                (apex[0], apex[1], GATE_HEAD + 4.0)]
        loft(m, tymp, [(p[0] + c * out * 0.12, p[1] + s * out * 0.12, p[2]) for p in tymp], "Slate")
        beam(m, (foot_l[0], foot_l[1], GATE_HEAD), (apex[0], apex[1], GATE_HEAD + 4.2), 0.6, 0.5,
             (c * out, s * out, 0.0), "Brass")
        beam(m, (apex[0], apex[1], GATE_HEAD + 4.2), (foot_r[0], foot_r[1], GATE_HEAD), 0.6, 0.5,
             (c * out, s * out, 0.0), "Brass")
    GATES.append({"name": name, "axis_deg": round(math.degrees(a), 3), "opening_m": 2 * GATE_HALF,
                  "clear_height_m": GATE_HEAD - FLOOR})
    TARGETS.append(("%s gate, outside" % name, c * (WALL_RO + 12), s * (WALL_RO + 12)))
    TARGETS.append(("%s gate, inside" % name, c * (WALL_RI - 6), s * (WALL_RI - 6)))


def build_postern(m, name, a):
    c, s = math.cos(a), math.sin(a)
    t = (-s, c)
    for side in (-1, 1):
        cx = c * (WALL_RO + 0.8) + t[0] * side * (POSTERN_HALF + 4.6)
        cy = s * (WALL_RO + 0.8) + t[1] * side * (POSTERN_HALF + 4.6)
        round_tower(m, cx, cy, 4.2, -1.5, 16.5, 5.5, cap="pointed", n=16, slits=4, collide=True)
    TARGETS.append(("%s postern" % name, c * (WALL_RO + 8), s * (WALL_RO + 8)))


# --------------------------------------------------------------------------------------------------
# Paving, the square, the fountain, avenues, ring road, parks
# --------------------------------------------------------------------------------------------------

def build_paving():
    m = M("Ground", "Site")
    # Ring road inside the wall (A-07: 12 m).
    annulus(m, RING_RI, WALL_RI + 0.05, FLOOR - 0.01, FLOOR + LAYER["Road"], "Road", n=128)
    annulus(m, RING_RI - 0.6, RING_RI, FLOOR - 0.01, FLOOR + LAYER["RingEdge"], "PavingLight", n=128)
    # Avenues (A-07: 16 m), wall to canal, across the bridges and the lagoon to the perimeter road.
    for name, a in gate_axes():
        spans = [(CANAL_RI - 0.3, ROAD_IN + 0.6)]
        if name == "North":
            spans = [(CANAL_RI - 0.3, ADMIN_Y0 + KNIT), (ADMIN_Y1 - KNIT, ROAD_IN + 0.6)]
        for d0, d1 in spans:
            seg(m, polar(d0, a), polar(d1, a), 2 * AVENUE_HALF, FLOOR - 0.01, FLOOR + LAYER["Avenue"],
                "PavingLight")
            d0, d1 = d0 + 0.15, d1 - 0.15     # the lines stop short of the avenue's own ends
            # A brass line down the middle and dark kerb strips, both flush inlays.
            seg(m, polar(d0, a), polar(d1, a), 0.4, FLOOR - 0.01, FLOOR + LAYER["Inlay"], "Brass")
            for side in (-1, 1):
                # Inset from the avenue's edge, so the strip's side and the avenue's side never share a plane.
                off = (-math.sin(a) * side * (AVENUE_HALF - 0.5), math.cos(a) * side * (AVENUE_HALF - 0.5))
                p0, p1 = polar(d0, a), polar(d1, a)
                seg(m, (p0[0] + off[0], p0[1] + off[1]), (p1[0] + off[0], p1[1] + off[1]), 0.8,
                    FLOOR - 0.01, FLOOR + LAYER["Inlay"], "Road")
    # Canal footbridges on the diagonals.
    for _n, a in postern_axes():
        seg(m, polar(CANAL_RI - 0.4, a), polar(CANAL_RO + 0.4, a), 6.6, FLOOR - 0.45, FLOOR + LAYER["Avenue"],
            "PavingLight", bottom="StoneDark")
    # Avenue bridge decks over the canal (the avenue inlay runs over them).
    for _n, a in gate_axes():
        seg(m, polar(CANAL_RI - 0.2, a), polar(CANAL_RO + 0.2, a), 2 * AVENUE_HALF + 0.6, FLOOR - 0.5,
            FLOOR - 0.005, "StoneDark")

    # The town square: rings and spokes, the concept's radial pattern.
    annulus(m, 26.0, 29.6, FLOOR - 0.01, FLOOR + LAYER["PlazaRing"], "PavingLight", n=64)
    annulus(m, 29.6, 30.0, FLOOR - 0.01, FLOOR + LAYER["Inlay"], "Brass", n=64)
    annulus(m, 10.2, 12.2, FLOOR - 0.01, FLOOR + LAYER["PlazaRing"], "PavingLight", n=48)
    for k in range(16):
        a = 2 * math.pi * k / 16 + math.pi / 16
        seg(m, polar(12.0, a), polar(26.2, a), 1.2, FLOOR - 0.01, FLOOR + LAYER["Spoke"], "PavingLight")


def build_fountain():
    m = M("Square", "Districts")
    m_current[0] = m
    w = M("Water", "Site")
    # Basin rim: a ring of convex segments, 0.7 m tall -- an obstacle, which is right.
    annulus(m, 8.4, 9.0, FLOOR - KNIT, FLOOR + 0.62, "StoneDark", n=32, collide=True)
    annulus(m, 8.3, 9.15, FLOOR + 0.6, FLOOR + 0.72, "Brass", n=32)
    frustum(m, 0, 0, 8.45, 8.45, FLOOR - 0.6, FLOOR + 0.05, 32, "LagoonBed")
    _ring_sheet(w, 0.0001, 8.42, FLOOR + 0.42, 32)
    WATER_SURFACES.append(("fountain basin", 0.0, FLOOR + 0.42))
    # Tiers: pedestal, a brass bowl with its own water, an obelisk and a lit finial.
    frustum(m, 0, 0, 2.4, 1.7, FLOOR, FLOOR + 2.7, 16, "Stone", collide=True)
    frustum(m, 0, 0, 1.7, 4.4, FLOOR + 2.6, FLOOR + 3.15, 24, "Brass", smooth=True)
    annulus(m, 4.05, 4.45, FLOOR + 3.1, FLOOR + 3.4, "Brass", n=24)
    _ring_sheet(w, 0.0001, 4.1, FLOOR + 3.3, 24)
    WATER_SURFACES.append(("fountain bowl", 0.0, FLOOR + 3.3))
    aabox(m, -0.8, 0.8, -0.8, 0.8, FLOOR + 3.1, FLOOR + 6.4, "Stone")
    aabox(m, -0.95, 0.95, -0.95, 0.95, FLOOR + 6.3, FLOOR + 6.6, "Brass")
    frustum(m, 0, 0, 0.75, 0.0, FLOOR + 6.5, FLOOR + 15.5, 4, "Brass", rot=math.pi / 4)
    sphere(m, 0, 0, FLOOR + 15.8, 0.45, "Light", 10, 6)
    # Benches facing the fountain.
    for k in range(8):
        a = 2 * math.pi * k / 8 + math.pi / 8
        p = polar(19.0, a)
        obox(m, p[0], p[1], 0.8, 4.0, FLOOR - KNIT, FLOOR + 0.48, a, "StoneDark", collide=True)
        obox(m, p[0], p[1], 0.9, 4.1, FLOOR + 0.46, FLOOR + 0.52, a, "Brass")
    FREE_ZONES.append(("circle", (0.0, 0.0, 9.5), "fountain"))


# (cx, cy, radius) -- placed by searching for space _clear accepts, and checked again where they are built.
PARKS = [
    (-36, 130, 15), (-152, 24, 13), (-140, -22, 11), (-30, -150, 14), (32, -86, 11),
    (150, 22, 12), (140, -24, 11), (32, 156, 12), (-150, 86, 9), (154, -78, 9),
    (-34, -86, 10), (-106, 138, 8), (84, 152, 8), (-98, -144, 8), (102, -142, 7),
]


def build_parks_and_trees(buildings):
    """Grass and trees where nothing else is, checked against every footprint and path."""
    m = M("Trees", "Props")
    g = M("Ground", "Site")
    trees = []
    for cx, cy, r in PARKS:
        _assert_clear("park", cx, cy, r + 1.0, buildings)
        frustum(g, cx, cy, r, r, FLOOR - 0.01, FLOOR + LAYER["Grass"], 32, "Grass")
        frustum(g, cx, cy, r + 0.5, r + 0.5, FLOOR - 0.01, FLOOR + LAYER["Grass"] - 0.004, 32, "StoneDark")
        n = max(3, int(r / 2.6))
        for k in range(n):
            a = 2 * math.pi * k / n + 0.4
            trees.append(polar(r * 0.62, a, cx, cy))
        if r >= 10:
            trees.append((cx, cy))
    # Avenue trees, either side, every 12 m where nothing is in the way.
    for name, a in gate_axes():
        c, s = math.cos(a), math.sin(a)
        t = (-s, c)
        d = CANAL_RO + 10.0
        while d < RING_RI - 6.0:
            for side in (-1, 1):
                p = (c * d + t[0] * side * (AVENUE_HALF + 2.6), s * d + t[1] * side * (AVENUE_HALF + 2.6))
                if _clear(p[0], p[1], 2.4, buildings, avenues=False):
                    trees.append(p)
            d += 12.0
    for x, y in trees:
        tree(m, x, y)
    return trees


def tree(m, x, y):
    frustum(m, x, y, 0.36, 0.24, FLOOR - KNIT, FLOOR + 3.4, 8, "Bark", collide=True)
    sphere(m, x, y, FLOOR + 5.0, 2.6, "Foliage", 10, 7, sz=0.9)
    sphere(m, x + 0.4, y - 0.3, FLOOR + 6.9, 1.7, "Foliage", 9, 6, sz=0.9)


def build_lamps(buildings):
    m = M("Lamps", "Props")
    spots = []
    for name, a in gate_axes():
        c, s = math.cos(a), math.sin(a)
        t = (-s, c)
        d = CANAL_RO + 4.0
        while d < WALL_RI - 4.0:
            for side in (-1, 1):
                p = (c * d + t[0] * side * (AVENUE_HALF + 0.7), s * d + t[1] * side * (AVENUE_HALF + 0.7))
                if _clear(p[0], p[1], 1.0, buildings, avenues=False):
                    spots.append(p)
            d += 18.0
    for k in range(48):
        a = 2 * math.pi * (k + 0.5) / 48
        if any(ang_dist(a, g) < math.radians(4) for _n, g in gate_axes()):
            continue
        if any(ang_dist(a, p) < math.radians(3) for _n, p in postern_axes()):
            continue
        spots.append(polar(RING_RI - 1.4, a))
    for k in range(8):
        spots.append(polar(32.4, 2 * math.pi * k / 8))
    for x, y in spots:
        lamp(m, x, y)


def lamp(m, x, y, h=5.6):
    aabox(m, x - 0.16, x + 0.16, y - 0.16, y + 0.16, FLOOR - KNIT, FLOOR + h, "Metal", collide=True)
    aabox(m, x - 0.32, x + 0.32, y - 0.32, y + 0.32, FLOOR - KNIT, FLOOR + 0.6, "StoneDark")
    frustum(m, x, y, 0.38, 0.30, FLOOR + h - 0.05, FLOOR + h + 0.7, 8, "Light")
    frustum(m, x, y, 0.46, 0.0, FLOOR + h + 0.65, FLOOR + h + 1.3, 8, "Brass")


# --------------------------------------------------------------------------------------------------
# Scenery: the closed buildings that make it a city (task 167)
# --------------------------------------------------------------------------------------------------
#
# Joe, 2 October: the concept is denser than the twelve buildings anybody can enter, and the gaps can be
# filled with "scenery we can't enter for now". Solid, collided, no doors: each is one hull, so nothing
# walks or flies through it, and the route check still has to reach every door past them.

SCENERY_GAP = 5.0          # from an enterable building, its forecourts and other scenery: a lane, not a slot
SCENERY_PARK_GAP = 3.0     # from a park's kerb
SCENERY_AVENUE_GAP = 6.5   # from an avenue's edge: its lamps and trees stand in the first five metres
SCENERY_PATH_HALF = 9.0    # either side of a postern causeway's line, kept open as a street
SCENERY_CANAL_GAP = 8.0    # from the canal's outer edge: the promenade round the square
SCENERY_SIZES = [(28, 18), (24, 16), (20, 14), (16, 16), (16, 12), (12, 12), (12, 9)]
SCENERY_HEIGHTS = [11.0, 14.0, 17.0, 12.5, 20.0, 15.5]
SCENERY_ROOFS = ["hip", "dome", "hip", "spires", "hip", "flat"]


def _rects_gap(a, b):
    """Clear distance between two axis-aligned rectangles; negative if they overlap."""
    dx = max(b[0] - a[1], a[0] - b[1])
    dy = max(b[2] - a[3], a[2] - b[3])
    if dx > 0 and dy > 0:
        return math.hypot(dx, dy)
    return max(dx, dy)


def _band_hit(rect, a, half):
    """Whether a rectangle reaches into the strip `half` either side of the ray from the centre at angle a."""
    c, s = math.cos(a), math.sin(a)
    corners = [(x, y) for x in (rect[0], rect[1]) for y in (rect[2], rect[3])]
    lats = [-x * s + y * c for x, y in corners]
    alongs = [x * c + y * s for x, y in corners]
    return max(alongs) > 0.0 and min(lats) < half and max(lats) > -half


def _scenery_clear(rect, buildings, placed):
    corners = [(x, y) for x in (rect[0], rect[1]) for y in (rect[2], rect[3])]
    if any(math.hypot(x, y) > DISTRICT_LIMIT for x, y in corners):
        return False
    if _rect_dist(0.0, 0.0, rect) < CANAL_RO + SCENERY_CANAL_GAP:
        return False
    for b in buildings:
        if _rects_gap(rect, b["rect"]) < SCENERY_GAP:
            return False
        if any(_rects_gap(rect, fc) < SCENERY_GAP for fc in b["forecourts"]):
            return False
    if any(_rects_gap(rect, other["rect"]) < SCENERY_GAP for other in placed):
        return False
    for cx, cy, r in PARKS:
        if _rect_dist(cx, cy, rect) < r + 0.5 + SCENERY_PARK_GAP:
            return False
    for kind, data, _label in FREE_ZONES:
        if kind == "circle" and _rect_dist(data[0], data[1], rect) < data[2]:
            return False
    if any(_band_hit(rect, a, AVENUE_HALF + SCENERY_AVENUE_GAP) for _n, a in gate_axes()):
        return False
    if any(_band_hit(rect, a, SCENERY_PATH_HALF) for _n, a in postern_axes()):
        return False
    return True


def plan_scenery(buildings):
    """Closed buildings wherever they fit, largest first, scanned in a fixed order so a rebuild is the same city."""
    placed = []
    for w, d in SCENERY_SIZES:
        for sw, sd in ((w, d), (d, w)) if w != d else ((w, d),):
            y = -DISTRICT_LIMIT
            while y <= DISTRICT_LIMIT:
                x = -DISTRICT_LIMIT
                while x <= DISTRICT_LIMIT:
                    rect = (x - sw / 2.0, x + sw / 2.0, y - sd / 2.0, y + sd / 2.0)
                    if _scenery_clear(rect, buildings, placed):
                        k = len(placed)
                        placed.append({"rect": rect, "height": SCENERY_HEIGHTS[k % len(SCENERY_HEIGHTS)],
                                       "roof": SCENERY_ROOFS[k % len(SCENERY_ROOFS)], "forecourts": []})
                    x += 2.0
                y += 2.0
    return placed


def build_scenery(scenery):
    m = M("Scenery", "Districts")
    m_current[0] = m
    for k, b in enumerate(scenery):
        x0, x1, y0, y1 = rect = b["rect"]
        h = b["height"]
        top = FLOOR + h
        # The block: solid, and the one hull a person or a ship meets.
        aabox(m, x0, x1, y0, y1, FLOOR - KNIT, top, "Stone", collide=True)
        # A dark plinth and a cornice, both proud of the wall, so it reads as built rather than extruded.
        aabox(m, x0 - 0.2, x1 + 0.2, y0 - 0.2, y1 + 0.2, FLOOR - KNIT, FLOOR + 0.9, "StoneDark")
        aabox(m, x0 - 0.35, x1 + 0.35, y0 - 0.35, y1 + 0.35, top - 0.9, top - 0.45, "StoneDark")
        # Windows: a row a storey, a bay every three metres, clear of the corners. Lit, like the concept.
        storeys = max(1, int((h - 2.0) / 3.5))
        for side in ("S", "N", "E", "W"):
            _o, _t, _n, length = side_frame(rect, side)
            bays = int((length - 3.6) / 3.0)
            if bays < 1:
                continue
            start = (length - (bays - 1) * 3.0) / 2.0
            for row in range(storeys):
                z0 = FLOOR + 1.6 + row * 3.5
                if z0 + 1.8 > top - 1.0:
                    break
                for i in range(bays):
                    mat = "Light" if (i + row + k) % 5 == 0 else "Glass"
                    on_face(rect, side, start + i * 3.0, 1.1, -0.06, 0.08, z0, z0 + 1.8, mat)
        cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        roof = b["roof"]
        if roof in ("hip", "spires"):
            # Slate, from inside the block's top out over its walls, to a short ridge.
            inset = min(x1 - x0, y1 - y0) / 2.0 - 1.0
            eave = [(x0 - 0.5, y0 - 0.5), (x1 + 0.5, y0 - 0.5), (x1 + 0.5, y1 + 0.5), (x0 - 0.5, y1 + 0.5)]
            ridge = [(x0 + inset, y0 + inset), (x1 - inset, y0 + inset), (x1 - inset, y1 - inset),
                     (x0 + inset, y1 - inset)]
            rise = min(6.0, inset * 0.75)
            loft(m, at_z(eave, top - 0.3), at_z(ridge, top + rise), "Slate")
            if roof == "spires":
                for px, py in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
                    pinnacle(m, px, py, 1.6, top - KNIT, top + 2.4, 3.5)
        elif roof == "dome":
            # A flat roof with a parapet, and a small blue glass dome on a drum: the concept's skyline.
            for side in ("S", "N", "E", "W"):
                on_face(rect, side, side_frame(rect, side)[3] / 2.0, side_frame(rect, side)[3] + 0.7,
                        -0.45, 0.25, top - KNIT, top + 1.0, "StoneDark")
            r = min(x1 - x0, y1 - y0) * 0.28
            drum_dome(m, cx, cy, r, top, 2.2, r * 1.1, n=16, ribs=8, windows=8)
        else:
            # Flat, with a parapet and a lit lantern: a roof garden, seen from the wall.
            for side in ("S", "N", "E", "W"):
                on_face(rect, side, side_frame(rect, side)[3] / 2.0, side_frame(rect, side)[3] + 0.7,
                        -0.45, 0.25, top - KNIT, top + 1.0, "StoneDark")
            lantern(m, cx, cy, top, 1.4, 2.0, 2.5)
    return scenery


# --------------------------------------------------------------------------------------------------
# Buildings
# --------------------------------------------------------------------------------------------------

def _rect_dist(px, py, rect):
    x0, x1, y0, y1 = rect
    dx = max(x0 - px, 0.0, px - x1)
    dy = max(y0 - py, 0.0, py - y1)
    return math.hypot(dx, dy)


def _clear(x, y, r, buildings, avenues=True):
    """True if a circle of radius r at (x, y) keeps off every footprint, forecourt, avenue and canal.

    avenues=False lets a lamp or a tree stand at an avenue's edge, which is where they belong.
    """
    for b in buildings:
        if _rect_dist(x, y, b["rect"]) < r:
            return False
        for fc in b["forecourts"]:
            if _rect_dist(x, y, fc) < r:
                return False
    d = math.hypot(x, y)
    if d - r < CANAL_RO + 0.8:
        return False
    if d + r > RING_RI - 0.8:
        return False
    for _n, a in gate_axes():
        along = x * math.cos(a) + y * math.sin(a)
        lat = -x * math.sin(a) + y * math.cos(a)
        if along > 0 and abs(lat) < AVENUE_HALF + (r + 0.2 if avenues else 0.3):
            return False
    for _n, a in postern_axes():
        along = x * math.cos(a) + y * math.sin(a)
        lat = -x * math.sin(a) + y * math.cos(a)
        if along > 0 and abs(lat) < 4.0 + r:
            return False
    return True


def _assert_clear(what, x, y, r, buildings):
    if not _clear(x, y, r, buildings):
        raise SystemExit("%s at (%.0f, %.0f) r %.0f overlaps a building, path, avenue or canal" % (what, x, y, r))


# A-07 station schedule, with the positions the round wall forced. Footprints are the schedule's.
#   key            name                        station role         cx     cy     w   d   h_wall  doors
ADMIN_Y0, ADMIN_Y1 = 50.0, 88.0
SCHEDULE = [
    ("Admin", "Centre Administration", "station_capital_hub", 0.0, 69.0, 60, 38, 14.0,
     [("S", 0.0, 8.0, 6.0), ("N", 0.0, 8.0, 6.0)],
     "A-07 (0,51) moved 18 m north so the canal and the south portico fit round the square"),
    ("Market", "Global Market Station", "market", -86.0, 100.0, 72, 44, 14.0,
     [("S", 0.0, 8.0, 6.0), ("E", 0.0, 6.0, 5.0)],
     "A-07 (-88,104) moved 2 m east and 4 m south to clear the round wall"),
    ("BankNorth", "Bank / Finance Station (Trading District)", "bank", -100.0, 43.0, 48, 30, 12.0,
     [("S", 0.0, 6.0, 5.5)], "A-07 North Depot, as drawn"),
    ("HQ", "Capital HQ Towers", "hq", 66.0, 118.0, 60, 48, 14.0,
     [("S", 0.0, 8.0, 6.5), ("W", 0.0, 6.0, 5.0)],
     "A-07 (72,110) moved 6 m west and 8 m north, around the hotel and the round wall"),
    ("Apartments", "Apartments / Residential Buildings", "apartments", 122.0, 78.0, 44, 30, 8.0,
     [("S", -8.0, 6.0, 4.6), ("W", 0.0, 5.0, 4.6)], "A-07 (136,75) moved 14 m west to clear the round wall"),
    ("Hotel", "Hotel", "hotel", 62.0, 50.0, 40, 26, 15.0,
     [("S", 0.0, 6.0, 5.0)], "A-07 (55,45) moved 7 m east, 5 m north; schedule's 40 m, not the drawing's 38"),
    ("Pub", "Pub", "pub", 110.0, 40.0, 30, 22, 8.0,
     [("S", 0.0, 5.0, 4.4)], "A-07 (97,45) moved 13 m east to make room for the hotel"),
    ("CraftingHall", "Crafting Station", "crafting", -88.0, -54.0, 72, 44, 12.0,
     [("N", 0.0, 8.0, 6.0), ("E", 0.0, 6.0, 5.0)], "A-07 Crafting Hall, as drawn"),
    ("CraftingGuild", "Crafting Career Building", "crafting_careers", -84.0, -116.0, 56, 34, 13.0,
     [("N", 0.0, 6.0, 5.5)], "A-07 (-92,-117) moved 8 m east to clear the round wall"),
    ("RefiningHall", "Refining Station", "refining", 92.0, -54.0, 72, 44, 12.0,
     [("N", 0.0, 8.0, 6.0), ("W", 0.0, 6.0, 5.0)], "A-07 Refining Hall, as drawn"),
    ("RefiningGuild", "Refining Career Building", "refining_careers", 50.0, -117.0, 56, 34, 13.0,
     [("N", 0.0, 6.0, 5.5)], "A-07 Refining Guild Hall, as drawn"),
    ("BankSouth", "Bank / Finance Station (Refining District)", "bank", 104.0, -104.0, 44, 28, 12.0,
     [("N", 0.0, 6.0, 5.5)], "A-07 South Depot (108,-108) moved 4 m in to clear the round wall"),
]
SCHEDULE_FOOTPRINTS = {   # the A-07 table, as published, so the built footprints can be checked against it
    "Admin": (60, 38), "Market": (72, 44), "BankNorth": (48, 30), "BankSouth": (44, 28),
    "CraftingHall": (72, 44), "RefiningHall": (72, 44), "CraftingGuild": (56, 34), "RefiningGuild": (56, 34),
    "HQ": (60, 48), "Hotel": (40, 26), "Apartments": (44, 30), "Pub": (30, 22),
}


def plan_buildings():
    out = []
    for key, name, role, cx, cy, w, d, h_wall, doors, note in SCHEDULE:
        rect = (cx - w / 2.0, cx + w / 2.0, cy - d / 2.0, cy + d / 2.0)
        rec = {"key": key, "name": name, "role": role, "rect": rect, "centre": (cx, cy), "size": (w, d),
               "h_wall": h_wall, "doors": [], "forecourts": [], "note": note, "services": [], "furniture": []}
        for side, off, dw, dh in doors:
            (sx, sy), (tx, ty), (nx, ny), length = side_frame(rect, side)
            s = length / 2.0 + off
            centre = (sx + tx * s, sy + ty * s)
            rec["doors"].append({"side": side, "s": s, "width": dw, "height": dh, "centre": centre,
                                 "normal": (nx, ny)})
            # Forecourt: the paved apron in front of every door, kept clear of trees and lamps.
            depth, half = 9.0, dw / 2.0 + 3.0
            corners = [(centre[0] + tx * a + nx * o, centre[1] + ty * a + ny * o)
                       for a, o in ((-half, 0.0), (half, 0.0), (half, depth), (-half, depth))]
            fx = [p[0] for p in corners]
            fy = [p[1] for p in corners]
            rec["forecourts"].append((min(fx), max(fx), min(fy), max(fy)))
        for x in (rect[0], rect[1]):
            for y in (rect[2], rect[3]):
                if math.hypot(x, y) > DISTRICT_LIMIT:
                    raise SystemExit("%s corner (%.0f, %.0f) is %.1f m out, past the %.0f m district limit"
                                     % (key, x, y, math.hypot(x, y), DISTRICT_LIMIT))
                if math.hypot(x, y) < CANAL_RO + 4.0:
                    raise SystemExit("%s corner (%.0f, %.0f) is inside the canal" % (key, x, y))
        out.append(rec)
    for i in range(len(out)):
        for j in range(i + 1, len(out)):
            a, b = out[i]["rect"], out[j]["rect"]
            gap = max(b[0] - a[1], a[0] - b[1], b[2] - a[3], a[2] - b[3])
            if gap < 3.0:
                raise SystemExit("%s and %s are %.1f m apart; nothing narrower than 3 m between buildings"
                                 % (out[i]["key"], out[j]["key"], gap))
    for b in out:
        for side, a in gate_axes():
            x0, x1, y0, y1 = b["rect"]
            if b["key"] == "Admin":
                continue
            # No building may stand on an avenue.
            if side in ("North", "South") and x0 < AVENUE_HALF and x1 > -AVENUE_HALF:
                if (side == "North" and y1 > 0) or (side == "South" and y0 < 0):
                    raise SystemExit("%s stands on the %s avenue" % (b["key"], side))
            if side in ("East", "West") and y0 < AVENUE_HALF and y1 > -AVENUE_HALF:
                if (side == "East" and x1 > 0) or (side == "West" and x0 < 0):
                    raise SystemExit("%s stands on the %s avenue" % (b["key"], side))
    return out


def shell(m, b, pilaster_bay=6.5):
    """Walls with doors, a hall inside, a roof and the facade every building shares.

    The hall is real: walls have thickness and a plaster face inside, the door is a gap in the wall,
    the ceiling is a slab the walls stop inside of. Everything above the hall is closed.
    """
    m_current[0] = m
    x0, x1, y0, y1 = b["rect"]
    h = b["h_wall"]
    T = WALL_T
    rect = b["rect"]

    def wall_mat(inside_normal):
        def pick(n):
            return "Interior" if (n[0] * inside_normal[0] + n[1] * inside_normal[1]) > 0.9 else "Stone"
        return pick

    sides = {
        "S": ((x0, x1), (y0, y0 + T), (0.0, 1.0)),
        "N": ((x0, x1), (y1 - T, y1), (0.0, -1.0)),
        "W": ((x0, x0 + T), (y0 + T - KNIT, y1 - T + KNIT), (1.0, 0.0)),
        "E": ((x1 - T, x1), (y0 + T - KNIT, y1 - T + KNIT), (-1.0, 0.0)),
    }
    for side, (xs, ys, inside) in sides.items():
        horizontal = side in ("S", "N")
        lo, hi = (xs if horizontal else ys)
        gaps = []
        for door in b["doors"]:
            if door["side"] != side:
                continue
            c = door["centre"][0] if horizontal else door["centre"][1]
            gaps.append((c - door["width"] / 2.0, c + door["width"] / 2.0, door["height"]))
        gaps.sort()
        cursor = lo
        for g0, g1, gh in gaps:
            _wall_piece(m, side, horizontal, xs, ys, cursor, g0, FLOOR - KNIT, h - 0.2, wall_mat(inside))
            _wall_piece(m, side, horizontal, xs, ys, g0 - KNIT, g1 + KNIT, FLOOR + gh, h - 0.2, wall_mat(inside),
                        collide=False)
            cursor = g1
        _wall_piece(m, side, horizontal, xs, ys, cursor, hi, FLOOR - KNIT, h - 0.2, wall_mat(inside))

    # Floor of the hall, running out through each doorway as a threshold.
    aabox(m, x0, x1, y0, y1, FLOOR - 0.01, FLOOR + LAYER["Floor"], "Floor")
    # Ceiling, reaching into the walls.
    aabox(m, x0 + T - KNIT, x1 - T + KNIT, y0 + T - KNIT, y1 - T + KNIT, H_HALL, H_HALL + 0.45, "Interior")
    # Ceiling light panels.
    nx = max(1, int((x1 - x0 - 2 * T) // 9))
    ny = max(1, int((y1 - y0 - 2 * T) // 9))
    for i in range(nx):
        for j in range(ny):
            cx = x0 + T + (i + 0.5) * (x1 - x0 - 2 * T) / nx
            cy = y0 + T + (j + 0.5) * (y1 - y0 - 2 * T) / ny
            aabox(m, cx - 2.2, cx + 2.2, cy - 0.7, cy + 0.7, H_HALL - 0.06, H_HALL + 0.05, "Light")
    # Roof slab; the walls stop inside it.
    aabox(m, x0, x1, y0, y1, h - 0.3, h + 0.3, "Stone", top="Slate")
    # Cornice, parapet and coping.
    for side in ("S", "N", "E", "W"):
        (sx, sy), (tx, ty), (nnx, nny), length = side_frame(rect, side)
        on_face(rect, side, length / 2.0, length + 0.6, -0.2, 0.32, h - 0.75, h - 0.1, "Brass")
        on_face(rect, side, length / 2.0, length, -0.5, 0.0, h + 0.2, h + 1.25, "Stone")
        on_face(rect, side, length / 2.0, length + 0.1, -0.56, 0.05, h + 1.2, h + 1.36, "Brass")
    # Plinth band and pilasters at street level -- these collide, they stand out from the wall.
    for side in ("S", "N", "E", "W"):
        (sx, sy), (tx, ty), (nnx, nny), length = side_frame(rect, side)
        door_spans = [(d["s"] - d["width"] / 2.0, d["s"] + d["width"] / 2.0) for d in b["doors"]
                      if d["side"] == side]
        cursor = -0.2
        for g0, g1 in sorted(door_spans):
            # Ends 0.2 m short of the opening, which is inside the door frame's jamb: its cut end is buried.
            if g0 - 0.2 - cursor > 0.2:
                on_face(rect, side, (cursor + g0 - 0.2) / 2.0, g0 - 0.2 - cursor, -0.1, 0.22, FLOOR - KNIT,
                        FLOOR + 1.0, "StoneDark", collide=True)
            cursor = g1 + 0.2
        if length + 0.2 - cursor > 0.2:
            on_face(rect, side, (cursor + length + 0.2) / 2.0, length + 0.2 - cursor, -0.1, 0.22,
                    FLOOR - KNIT, FLOOR + 1.0, "StoneDark", collide=True)
        positions = []
        k = 1
        while k * pilaster_bay < length - 1.5:
            positions.append(k * pilaster_bay)
            k += 1
        positions = [p for p in positions if all(abs(p - (g0 + g1) / 2.0) > (g1 - g0) / 2.0 + 1.6
                                                  for g0, g1 in door_spans)]
        for p in positions:
            on_face(rect, side, p, 1.0, -0.1, 0.48, FLOOR + 0.9, h - 0.65, "Stone", collide=True)
            on_face(rect, side, p, 1.3, -0.1, 0.6, h - 1.5, h - 0.7, "Brass")
        # Windows centred in the bays between pilasters (and the corners), two bands.
        stops = [0.0] + positions + [length]
        for a, c in zip(stops, stops[1:]):
            mid = (a + c) / 2.0
            if c - a < 2.6:
                continue
            if any(abs(mid - (g0 + g1) / 2.0) < (g1 - g0) / 2.0 + 1.2 for g0, g1 in door_spans):
                continue
            lancet(rect, side, mid, 1.3, FLOOR + 1.9, FLOOR + 5.0)
            if h >= 11.0:
                lancet(rect, side, mid, 1.5, H_HALL + 1.4, h - 2.6)
    # Doors: brass frames, canopies, thresholds, and the doors' records.
    for door in b["doors"]:
        side, s, dw, dh = door["side"], door["s"], door["width"], door["height"]
        # The frame stands 3 cm clear of the opening and its head hangs 6 cm below the lintel, so none
        # of its faces lies in the plane of the wall's cut end or the lintel's underside.
        on_face(rect, side, s - dw / 2.0 - 0.38, 0.7, -0.1, 0.34, FLOOR - KNIT, FLOOR + dh + 0.7, "Brass",
                collide=True)
        on_face(rect, side, s + dw / 2.0 + 0.38, 0.7, -0.1, 0.34, FLOOR - KNIT, FLOOR + dh + 0.7, "Brass",
                collide=True)
        on_face(rect, side, s, dw + 0.05, -0.1, 0.34, FLOOR + dh - 0.06, FLOOR + dh + 0.7, "Brass")
        on_face(rect, side, s, dw + 3.0, -0.1, 2.4, FLOOR + dh + 0.9, FLOOR + dh + 1.25, "Slate")
        on_face(rect, side, s, dw + 3.2, 2.3, 2.5, FLOOR + dh + 0.85, FLOOR + dh + 1.3, "Brass")
        cx, cy = door["centre"]
        nx_, ny_ = door["normal"]
        DOORS.append({"building": b["key"], "side": side, "centre_m": [round(cx, 3), round(cy, 3), FLOOR],
                      "width_m": dw, "height_m": dh, "outward": [nx_, ny_]})
        TARGETS.append(("%s %s door, outside" % (b["key"], side), cx + nx_ * 4.0, cy + ny_ * 4.0))
        TARGETS.append(("%s %s door, inside" % (b["key"], side), cx - nx_ * 3.5, cy - ny_ * 3.5))
    # Forecourt paving.
    g = M("Ground", "Site")
    for fc in b["forecourts"]:
        aabox(g, fc[0], fc[1], fc[2], fc[3], FLOOR - 0.01, FLOOR + LAYER["Path"], "PavingLight")
    # Negative control: the middle of a wall is not somewhere anybody stands.
    NEGATIVES.append(("inside %s's south wall" % b["key"], x0 + 2.0, y0 + T / 2.0))


def _wall_piece(m, side, horizontal, xs, ys, a, c, z0, z1, mat, collide=True):
    if c - a < 1e-3:
        return
    if horizontal:
        aabox(m, a, c, ys[0], ys[1], z0, z1, "Stone", side=mat, collide=collide)
    else:
        aabox(m, xs[0], xs[1], a, c, z0, z1, "Stone", side=mat, collide=collide)


# ---- furniture: the plan set's kit of parts -------------------------------------------------------

def _service(b, service, label, stand, facing, extra=None):
    rec = {"building": b["key"], "service": service, "label": label,
           "stand_m": [round(stand[0], 3), round(stand[1], 3), FLOOR], "facing_deg": round(facing, 2),
           "reach_m": 2.5}
    if extra:
        rec.update(extra)
    SERVICE_POINTS.append(rec)
    b["services"].append(service)
    TARGETS.append(("%s: %s" % (b["key"], label), stand[0], stand[1]))


def against_wall(b, side, s, w, depth, h, mat, top_mat=None, gap=0.0):
    """A block flush to the inside of a wall (or `gap` off it). Returns its front centre and normal."""
    x0, x1, y0, y1 = b["rect"]
    T = WALL_T
    inner = (x0 + T, x1 - T, y0 + T, y1 - T)
    m = m_current[0]
    if side == "N":
        cx = inner[0] + s
        r = (cx - w / 2, cx + w / 2, inner[3] - gap - depth, inner[3] - gap + (KNIT if gap == 0 else 0))
        front, n = (cx, r[2]), (0.0, -1.0)
    elif side == "S":
        cx = inner[0] + s
        r = (cx - w / 2, cx + w / 2, inner[2] + gap - (KNIT if gap == 0 else 0), inner[2] + gap + depth)
        front, n = (cx, r[3]), (0.0, 1.0)
    elif side == "W":
        cy = inner[2] + s
        r = (inner[0] + gap - (KNIT if gap == 0 else 0), inner[0] + gap + depth, cy - w / 2, cy + w / 2)
        front, n = (r[1], cy), (1.0, 0.0)
    else:
        cy = inner[2] + s
        r = (inner[1] - gap - depth, inner[1] - gap + (KNIT if gap == 0 else 0), cy - w / 2, cy + w / 2)
        front, n = (r[0], cy), (-1.0, 0.0)
    aabox(m, r[0], r[1], r[2], r[3], FLOOR - KNIT, FLOOR + h, mat, top=top_mat, collide=True)
    b["furniture"].append(r)
    return front, n, r


def wall_panel(b, side, centre, width, z0, z1, mat):
    """A lit panel on the inside face of a wall: 4 cm into the room, 6 cm into the wall."""
    x0, x1, y0, y1 = b["rect"]
    T = WALL_T
    m = m_current[0]
    if side == "N":
        aabox(m, centre - width / 2, centre + width / 2, y1 - T - 0.04, y1 - T + 0.06, z0, z1, mat)
    elif side == "S":
        aabox(m, centre - width / 2, centre + width / 2, y0 + T - 0.06, y0 + T + 0.04, z0, z1, mat)
    elif side == "E":
        aabox(m, x1 - T - 0.04, x1 - T + 0.06, centre - width / 2, centre + width / 2, z0, z1, mat)
    else:
        aabox(m, x0 + T - 0.06, x0 + T + 0.04, centre - width / 2, centre + width / 2, z0, z1, mat)


def counter(b, side, s, w, label, service, extra=None):
    """F/R/Q-style counter, 1.1 m high, flush to a wall, with a lit panel on the wall behind it."""
    front, n, r = against_wall(b, side, s, w, 2.5 if w >= 6 else 1.6, 1.1, "StoneDark", top_mat="Brass")
    centre = (r[0] + r[1]) / 2 if side in ("N", "S") else (r[2] + r[3]) / 2
    wall_panel(b, side, centre, w * 0.7, FLOOR + 2.0, FLOOR + 3.6, "Light")
    stand = (front[0] + n[0] * 1.1, front[1] + n[1] * 1.1)
    _service(b, service, label, stand, math.degrees(math.atan2(-n[1], -n[0])), extra)


def freestanding(b, x, y, w, depth, facing, label, service, extra=None):
    """A counter standing in the room, `facing` the way its customers stand. Q: 3.0 x 2.5 m."""
    m = m_current[0]
    obox(m, x, y, depth, w, FLOOR - KNIT, FLOOR + 1.1, facing, "StoneDark", top="Brass", collide=True)
    b["furniture"].append((x - 2.0, x + 2.0, y - 2.0, y + 2.0))
    c, s = math.cos(facing), math.sin(facing)
    obox(m, x - c * depth * 0.25, y - s * depth * 0.25, 0.3, w * 0.6, FLOOR + 1.05, FLOOR + 2.4, facing, "Light")
    stand = (x + c * (depth / 2 + 1.1), y + s * (depth / 2 + 1.1))
    _service(b, service, label, stand, math.degrees(facing + math.pi), extra)


def terminal(b, x, y, facing, label):
    """M: a market terminal, 2.0 x 1.0 m, a lit screen on a pedestal."""
    m = m_current[0]
    obox(m, x, y, 1.0, 2.0, FLOOR - KNIT, FLOOR + 1.05, facing, "Metal", collide=True)
    b["furniture"].append((x - 1.2, x + 1.2, y - 1.2, y + 1.2))
    c, s = math.cos(facing), math.sin(facing)
    obox(m, x - c * 0.1, y - s * 0.1, 0.18, 1.7, FLOOR + 1.0, FLOOR + 1.9, facing, "Glass")
    obox(m, x, y, 1.1, 2.1, FLOOR + 0.98, FLOOR + 1.06, facing, "Brass")
    stand = (x + c * 1.5, y + s * 1.5)
    _service(b, "market", label, stand, math.degrees(facing + math.pi))


def bay(b, side, s, label, skills):
    """J: an industry bay, 7.0 x 3.0 m: a machine body, a hood, lit vents."""
    front, n, r = against_wall(b, side, s, 7.0, 3.0, 2.2, "Metal", top_mat="Metal")
    m = m_current[0]
    x0, x1, y0, y1 = r
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    aabox(m, cx - 1.2, cx + 1.2, cy - 0.6, cy + 0.6, FLOOR + 2.1, FLOOR + 3.4, "Brass")
    if side in ("N", "S"):
        aabox(m, x0 + 0.5, x1 - 0.5, front[1] - 0.05, front[1] + 0.05, FLOOR + 1.2, FLOOR + 1.6, "Light")
    else:
        aabox(m, front[0] - 0.05, front[0] + 0.05, y0 + 0.5, y1 - 0.5, FLOOR + 1.2, FLOOR + 1.6, "Light")
    stand = (front[0] + n[0] * 1.2, front[1] + n[1] * 1.2)
    _service(b, "industry", label, stand, math.degrees(math.atan2(-n[1], -n[0])), {"skills": skills})


def racking(b, side, s, length, label):
    """H: hangar racking, a 1.5 m deep run of shelving against a wall."""
    front, n, r = against_wall(b, side, s, length, 1.5, 3.0, "Metal")
    m = m_current[0]
    x0, x1, y0, y1 = r
    for k in range(1, 4):
        z = FLOOR + k * 0.8
        aabox(m, x0 - 0.04, x1 + 0.04, y0 - 0.04, y1 + 0.04, z, z + 0.08, "Brass")
    stand = (front[0] + n[0] * 1.2, front[1] + n[1] * 1.2)
    _service(b, "storage", label, stand, math.degrees(math.atan2(-n[1], -n[0])))


def table(m, x, y, r=0.7):
    frustum(m, x, y, 0.22, 0.22, FLOOR - KNIT, FLOOR + 0.72, 8, "Metal", collide=True)
    frustum(m, x, y, r, r, FLOOR + 0.7, FLOOR + 0.78, 16, "Brass")


def interior_detail(b, bay=6.5):
    """Pilasters, a dado and a floor border inside the hall, so a room reads as designed, not as a box.

    Run after the furniture, because a pilaster behind a counter is a slot nobody can reach and a pilaster
    through one is a fault on sight: anything within 1.5 m of a piece of furniture is left out.
    """
    m = m_current[0]
    rect = b["rect"]
    T = WALL_T
    for side in ("S", "N", "E", "W"):
        (sx, sy), (tx, ty), (nx, ny), length = side_frame(rect, side)
        door_spans = [(d["s"] - d["width"] / 2.0, d["s"] + d["width"] / 2.0) for d in b["doors"]
                      if d["side"] == side]
        k = 1
        while k * bay < length - 1.5:
            s_ = k * bay
            k += 1
            if any(abs(s_ - (g0 + g1) / 2.0) < (g1 - g0) / 2.0 + 1.6 for g0, g1 in door_spans):
                continue
            px, py = sx + tx * s_ - nx * (T + 0.2), sy + ty * s_ - ny * (T + 0.2)
            if any(_rect_dist(px, py, f) < 1.5 for f in b["furniture"]):
                continue
            on_face(rect, side, s_, 0.9, -T - 0.4, -T + 0.1, FLOOR - KNIT, H_HALL + 0.1, "Stone", collide=True)
            on_face(rect, side, s_, 1.15, -T - 0.52, -T + 0.1, H_HALL - 0.75, H_HALL - 0.15, "Brass")
        # The dado runs round the room and stops at each door rather than crossing it.
        # 10 cm short of each opening, so its end is never in the plane of the wall's cut end.
        cursor = T - 0.1
        for g0, g1 in [(a - 0.1, c + 0.1) for a, c in sorted(door_spans)] + [(length - T + 0.1, length)]:
            if g0 - cursor > 0.3:
                on_face(rect, side, (cursor + g0) / 2.0, g0 - cursor, -T - 0.06, -T + 0.05, FLOOR + 1.0,
                        FLOOR + 1.22, "StoneDark")
            cursor = g1
        on_face(rect, side, length / 2.0, length - 2 * T + 0.2, -T - 1.0, -T + 0.05, FLOOR - 0.01,
                FLOOR + LAYER["Path"], "PavingLight")


# ---- each building's own look ----------------------------------------------------------------------

def b_admin(b):
    m = M("Admin", "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    cx, cy = b["centre"]
    h = b["h_wall"]
    top = drum_dome(m, cx, cy, 12.0, h + 0.2, 6.0, 10.5, "GlassDome", n=32, ribs=16, windows=16,
                    lantern_r=2.2, spire_h=8.0)
    for px in (x0 + 2.5, x1 - 2.5):
        for py in (y0 + 2.5, y1 - 2.5):
            pinnacle(m, px, py, 2.2, h + 0.2, h + 6.0, 4.5)
    # South portico: six columns, entablature and a pediment. Columns stand on the forecourt.
    for side, yface, out in (("S", y0, -1.0), ("N", y1, 1.0)):
        ycol = yface + out * 4.0
        for k in range(6):
            x = cx - 12.5 + k * 5.0
            if abs(x - cx) < 4.0:
                continue
            frustum(m, x, ycol, 0.7, 0.62, FLOOR - KNIT, FLOOR + 9.6, 16, "Stone", smooth=True, collide=True)
            frustum(m, x, ycol, 0.95, 0.95, FLOOR - KNIT, FLOOR + 0.6, 16, "StoneDark", collide=True)
            frustum(m, x, ycol, 0.9, 0.9, FLOOR + 9.5, FLOOR + 10.1, 16, "Brass")
        yy0, yy1 = sorted((yface - out * 0.2, ycol + out * 1.2))
        aabox(m, cx - 14.0, cx + 14.0, yy0, yy1, FLOOR + 10.0, FLOOR + 11.6, "Stone")
        aabox(m, cx - 14.3, cx + 14.3, yy0 - 0.15, yy1 + 0.15, FLOOR + 11.5, FLOOR + 11.9, "Brass")
        ped = [(cx - 14.0, yy0, FLOOR + 11.85), (cx + 14.0, yy0, FLOOR + 11.85), (cx, yy0, FLOOR + 16.0)]
        loft(m, ped, [(p[0], yy1, p[2]) for p in ped], "Stone", side="Slate")
    # Inside: quest 7 ends four metres in from the south door (plan set A-02 note 2, carried into A-07),
    # beside the straight walk from the square through the hall to the north avenue, not across it.
    m_current[0] = m
    freestanding(b, cx - 10.0, y0 + WALL_T + 4.0 + 1.25, 3.0, 2.5, -math.pi / 2, "Arrivals and quest stand",
                 "quests")
    counter(b, "N", 8.0, 8.0, "Registry and insurance", "registry", {"available": False,
                                                                      "note": "ADR-0006 payouts are M6"})
    interior_detail(b)
    return top


def b_market(b):
    m = M("Market", "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    cx, cy = b["centre"]
    h = b["h_wall"]
    drum_dome(m, cx, cy, 14.0, h + 0.2, 5.0, 12.0, "GlassDome", n=32, ribs=24, windows=20, lantern_r=2.5,
              spire_h=5.5)
    for px, py in ((x0 + 4.5, y0 + 4.5), (x1 - 4.5, y0 + 4.5), (x0 + 4.5, y1 - 4.5), (x1 - 4.5, y1 - 4.5)):
        frustum(m, px, py, 3.4, 3.4, h + 0.2, h + 6.0, 16, "Stone", smooth=True)
        frustum(m, px, py, 3.6, 3.6, h + 5.9, h + 6.3, 16, "Brass", smooth=True)
        dome(m, px, py, 3.5, h + 6.2, 3.4, "GlassDome", n=16, bands=6, ribs=8)
        frustum(m, px, py, 0.25, 0.0, h + 9.4, h + 12.0, 6, "Brass")
    m_current[0] = m
    # Six terminals (A-07: "Market Hall, 6 terminals"), three each side of the hall's axis.
    for k in range(3):
        terminal(b, x0 + 14.0 + k * 9.0, y1 - WALL_T - 4.0, -math.pi / 2, "Market terminal %d" % (k + 1))
        terminal(b, x1 - 14.0 - k * 9.0, y1 - WALL_T - 4.0, -math.pi / 2, "Market terminal %d" % (k + 4))
    # A lit system plate in the floor: the hall's centre, the plan set's "you are here".
    g = M("Market", "Districts")
    frustum(g, cx, cy - 2.0, 4.0, 4.0, FLOOR - 0.01, FLOOR + LAYER["Path"], 32, "Brass")
    frustum(g, cx, cy - 2.0, 3.2, 3.2, FLOOR - 0.01, FLOOR + LAYER["Avenue"], 32, "Light")
    interior_detail(b)


def b_bank(b, key):
    m = M(key, "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    cx, cy = b["centre"]
    h = b["h_wall"]
    drum_dome(m, cx, cy, 7.0, h + 0.2, 3.2, 6.5, "Slate", n=24, ribs=12, windows=10, lantern_r=1.4, spire_h=3.0)
    for px in (x0 + 2.0, x1 - 2.0):
        for py in (y0 + 2.0, y1 - 2.0):
            pinnacle(m, px, py, 1.8, h + 0.2, h + 4.4, 3.2)
    door = b["doors"][0]
    face_y = y0 if door["side"] == "S" else y1
    out = -1.0 if door["side"] == "S" else 1.0
    ycol = face_y + out * 3.6
    for k in range(6):
        x = cx - 10.0 + k * 4.0
        if abs(x - cx) < 3.5:
            continue
        frustum(m, x, ycol, 0.6, 0.52, FLOOR - KNIT, FLOOR + 8.2, 16, "Stone", smooth=True, collide=True)
        frustum(m, x, ycol, 0.8, 0.8, FLOOR + 8.1, FLOOR + 8.6, 16, "Brass")
    yy0, yy1 = sorted((face_y - out * 0.2, ycol + out * 1.0))
    aabox(m, cx - 11.0, cx + 11.0, yy0, yy1, FLOOR + 8.5, FLOOR + 9.9, "Stone")
    ped = [(cx - 11.0, yy0, FLOOR + 9.85), (cx + 11.0, yy0, FLOOR + 9.85), (cx, yy0, FLOOR + 13.0)]
    loft(m, ped, [(p[0], yy1, p[2]) for p in ped], "Stone", side="Slate")
    m_current[0] = m
    back = "N" if door["side"] == "S" else "S"
    inner_w = (x1 - x0) - 2 * WALL_T
    counter(b, back, inner_w / 2.0, 8.0, "Bank counter", "storage")
    racking(b, "W", ((y1 - y0) - 2 * WALL_T) / 2.0, (y1 - y0) - 2 * WALL_T - 6.0, "Vault racking, west")
    racking(b, "E", ((y1 - y0) - 2 * WALL_T) / 2.0, (y1 - y0) - 2 * WALL_T - 6.0, "Vault racking, east")
    interior_detail(b)


def b_hq(b):
    m = M("HQ", "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    cx, cy = b["centre"]
    h = b["h_wall"]
    # The landmark: a central tower to 46 m and a spire to 62 (A-07 asked 45 m; see deviations).
    s = 14.0
    aabox(m, cx - s / 2, cx + s / 2, cy - s / 2, cy + s / 2, h + 0.2, 46.0, "Stone")
    for zb in (24.0, 34.0, 44.0):
        aabox(m, cx - s / 2 - 0.25, cx + s / 2 + 0.25, cy - s / 2 - 0.25, cy + s / 2 + 0.25, zb, zb + 0.5, "Brass")
    trect = (cx - s / 2, cx + s / 2, cy - s / 2, cy + s / 2)
    m_current[0] = m
    for side in ("S", "N", "E", "W"):
        for k in (-1, 1):
            on_face(trect, side, s / 2 + k * 3.6, 0.35, -0.05, 0.12, h + 1.0, 45.6, "Light")
        for z0, z1 in ((h + 3.0, 22.5), (26.0, 32.5), (36.0, 42.5)):
            lancet(trect, side, s / 2, 2.0, z0, z1)
    frustum(m, cx, cy, 8.2, 8.2, 45.9, 49.0, 8, "Stone", rot=math.pi / 8)
    frustum(m, cx, cy, 8.5, 8.5, 48.9, 49.4, 8, "Brass", rot=math.pi / 8)
    for k in range(8):
        a = 2 * math.pi * (k + 0.5) / 8
        p = polar(7.95, a, cx, cy)
        obox(m, p[0], p[1], 0.22, 1.6, 46.4, 48.6, a, "Glass")
    frustum(m, cx, cy, 6.6, 0.0, 49.3, 62.0, 8, "Slate", rot=math.pi / 8)
    for k in range(8):
        a = 2 * math.pi * k / 8 + math.pi / 8
        beam(m, (cx + 6.7 * math.cos(a), cy + 6.7 * math.sin(a), 49.3), (cx, cy, 61.6), 0.28, 0.28,
             (math.cos(a), math.sin(a), 0.4), "Brass")
    frustum(m, cx, cy, 0.3, 0.0, 61.5, 65.0, 6, "Brass")
    sphere(m, cx, cy, 62.3, 0.55, "Light", 10, 6)
    # Four corner towers to 30 m, spires to 40.
    for px, py in ((x0 + 5.0, y0 + 5.0), (x1 - 5.0, y0 + 5.0), (x0 + 5.0, y1 - 5.0), (x1 - 5.0, y1 - 5.0)):
        aabox(m, px - 4.0, px + 4.0, py - 4.0, py + 4.0, h + 0.2, 30.0, "Stone")
        aabox(m, px - 4.25, px + 4.25, py - 4.25, py + 4.25, 29.9, 30.5, "Brass")
        r4 = (px - 4.0, px + 4.0, py - 4.0, py + 4.0)
        for side in ("S", "N", "E", "W"):
            for sv in (0.75, 7.25):
                on_face(r4, side, sv, 0.3, -0.05, 0.12, h + 1.0, 29.6, "Light")
            lancet(r4, side, 4.0, 1.4, 19.0, 26.5, frame=False)
        frustum(m, px, py, 4.6, 0.0, 30.4, 40.0, 4, "Slate", rot=math.pi / 4)
        frustum(m, px, py, 0.2, 0.0, 39.6, 42.0, 6, "Brass")
        for qx in (px - 4.0, px + 4.0):
            for qy in (py - 4.0, py + 4.0):
                pinnacle(m, qx, qy, 0.9, 30.4, 32.0, 2.4)
    # Buttresses with pinnacles along the long sides.
    for k in range(1, 4):
        x = x0 + k * (x1 - x0) / 4.0
        for yface, out in ((y0, -1.0), (y1, 1.0)):
            yy0, yy1 = sorted((yface - out * KNIT, yface + out * 1.6))
            if any(d["side"] in ("S", "N") and abs(d["centre"][0] - x) < d["width"] / 2 + 2.0 and
                   ((d["side"] == "S") == (out < 0)) for d in b["doors"]):
                continue
            aabox(m, x - 0.7, x + 0.7, yy0, yy1, FLOOR - KNIT, h + 1.8, "Stone", collide=True)
            pinnacle(m, x, (yy0 + yy1) / 2, 1.1, h + 1.8, h + 3.2, 2.6)
    m_current[0] = m
    counter(b, "N", ((x1 - x0) - 2 * WALL_T) / 2.0, 8.0, "Faction supply counter", "faction",
            {"available": False, "note": "the faction supply order was retired on 6 September (task 148)"})
    counter(b, "E", ((y1 - y0) - 2 * WALL_T) / 2.0 + 6.0, 8.0, "Registry desk", "registry",
            {"available": False, "note": "ADR-0006 and ADR-0009 are M6"})
    interior_detail(b)


def b_apartments(b):
    m = M("Apartments", "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    h = b["h_wall"]
    blocks = [(x0 + 0.5, x0 + 14.0, 26.0), (x0 + 15.0, x0 + 29.0, 30.0), (x0 + 30.0, x1 - 0.5, 23.0)]
    m_current[0] = m
    for bx0, bx1, top in blocks:
        r = (bx0, bx1, y0 + 2.0, y1 - 2.0)
        aabox(m, bx0, bx1, y0 + 2.0, y1 - 2.0, h + 0.2, top, "Stone", top="Slate")
        aabox(m, bx0 - 0.25, bx1 + 0.25, y0 + 1.75, y1 - 1.75, top - 0.1, top + 0.35, "Brass")
        for side in ("S", "N", "E", "W"):
            length = side_frame(r, side)[3]
            cols = max(1, int(length // 3.2))
            z = h + 1.6
            while z + 2.0 < top - 0.8:
                for k in range(cols):
                    s = (k + 0.5) * length / cols
                    on_face(r, side, s, 1.3, -0.05, 0.07, z, z + 1.7,
                            "LightWarm" if (k + int(z)) % 3 == 0 else "Glass")
                on_face(r, side, length / 2.0, length, -0.05, 0.50, z - 0.35, z - 0.15, "StoneDark")
                z += 3.2
        dome(m, (bx0 + bx1) / 2, (y0 + y1) / 2, 3.0, top + 0.3, 3.2, "GlassDome", n=16, bands=6, ribs=8)
        frustum(m, (bx0 + bx1) / 2, (y0 + y1) / 2, 0.2, 0.0, top + 3.3, top + 5.5, 6, "Brass")
    m_current[0] = m
    counter(b, "N", 30.0, 4.0, "Residents' desk", "housing",
            {"available": False, "note": "player housing is not designed yet"})
    interior_detail(b)


def b_hotel(b):
    m = M("Hotel", "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    cx, cy = b["centre"]
    h = b["h_wall"]
    aabox(m, cx - 8.0, cx + 8.0, y0 + 0.5, y1 - 0.5, h + 0.2, 21.0, "Stone", top="Slate")
    aabox(m, cx - 8.3, cx + 8.3, y0 + 0.2, y1 - 0.2, 20.9, 21.4, "Brass")
    r = (cx - 8.0, cx + 8.0, y0 + 0.5, y1 - 0.5)
    m_current[0] = m
    for side in ("S", "N"):
        for k in range(4):
            lancet(r, side, 2.5 + k * 3.66, 1.2, h + 1.6, 20.0, mat="LightWarm" if k % 2 else "Glass", frame=True)
    drum_dome(m, cx, cy, 6.0, 21.2, 2.6, 6.0, "GlassDome", n=24, ribs=12, windows=8, lantern_r=1.3, spire_h=3.5)
    for px in (x0 + 1.8, x1 - 1.8):
        for py in (y0 + 1.8, y1 - 1.8):
            pinnacle(m, px, py, 1.6, h + 0.2, h + 3.6, 2.6)
    # A glass and brass canopy over the door.
    door = b["doors"][0]
    for k in (-1, 1):
        frustum(m, door["centre"][0] + k * 5.0, y0 - 5.0, 0.22, 0.22, FLOOR - KNIT, FLOOR + 5.1, 8, "Brass",
                collide=True)
    aabox(m, door["centre"][0] - 6.0, door["centre"][0] + 6.0, y0 - 6.0, y0 + 0.1, FLOOR + 5.05, FLOOR + 5.35,
          "GlassDome")
    m_current[0] = m
    counter(b, "N", ((x1 - x0) - 2 * WALL_T) / 2.0, 6.0, "Reception", "lodging",
            {"available": False, "note": "transient rooms are not designed yet"})
    interior_detail(b)


def b_pub(b):
    m = M("Pub", "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    cx, cy = b["centre"]
    h = b["h_wall"]
    drum_dome(m, cx, cy, 7.0, h + 0.2, 2.0, 6.5, "Slate", n=24, ribs=12, windows=10, profile="onion",
              lantern_r=1.2, spire_h=2.5)
    for px in (x0 + 1.5, x1 - 1.5):
        for py in (y0 + 1.5, y1 - 1.5):
            pinnacle(m, px, py, 1.4, h + 0.2, h + 2.6, 2.0)
    m_current[0] = m
    front, n, r = against_wall(b, "N", ((x1 - x0) - 2 * WALL_T) / 2.0, 12.0, 1.4, 1.1, "Bark", top_mat="Brass")
    wall_panel(b, "N", (r[0] + r[1]) / 2, 10.0, FLOOR + 1.6, FLOOR + 3.4, "LightWarm")
    _service(b, "social", "Bar", (front[0], front[1] - 1.1), 90.0)
    for tx in (x0 + 6.0, x1 - 6.0):
        for ty in (y0 + 5.5, y0 + 10.5):
            table(m, tx, ty)
            b["furniture"].append((tx - 1.0, tx + 1.0, ty - 1.0, ty + 1.0))
    interior_detail(b)


def b_hall(b, key, skills, style):
    """Crafting and Refining Stations: four industry bays each (A-07)."""
    m = M(key, "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    cx, cy = b["centre"]
    h = b["h_wall"]
    if style == "crafting":
        drum_dome(m, cx, cy, 11.0, h + 0.2, 3.5, 9.0, "Slate", n=28, ribs=14, windows=14, lantern_r=2.0,
                  spire_h=4.0)
        for dx in (-23.0, 23.0):
            drum_dome(m, cx + dx, cy, 5.5, h + 0.2, 2.0, 4.5, "GlassDome", n=20, ribs=10, windows=8,
                      lantern_r=1.0, spire_h=2.0)
        for px, py in ((x0 + 3.0, y1 - 3.0), (x1 - 3.0, y1 - 3.0), (x0 + 3.0, y0 + 3.0), (x1 - 3.0, y0 + 3.0)):
            aabox(m, px - 1.2, px + 1.2, py - 1.2, py + 1.2, h + 0.2, 26.0, "StoneDark")
            aabox(m, px - 1.45, px + 1.45, py - 1.45, py + 1.45, 25.9, 26.5, "Brass")
            aabox(m, px - 0.9, px + 0.9, py - 0.9, py + 0.9, 26.4, 26.9, "LightWarm")
    else:
        drum_dome(m, cx - 6.0, cy + 4.0, 9.0, h + 0.2, 3.0, 7.0, "Slate", n=24, ribs=12, windows=12,
                  lantern_r=1.6, spire_h=3.0)
        for k, dx in enumerate((-26.0, -15.0, 18.0, 28.0)):
            sx_, sy_ = cx + dx, y0 + 7.0
            r = 3.6 if k % 2 == 0 else 3.0
            top = 31.0 if k % 2 == 0 else 26.0
            frustum(m, sx_, sy_, r, r * 0.86, h + 0.2, top, 20, "Stone", smooth=True)
            for zb in (h + 4.0, h + 9.0, top - 2.0):
                frustum(m, sx_, sy_, r + 0.2, r + 0.2, zb, zb + 0.45, 20, "Brass", smooth=True)
            dome(m, sx_, sy_, r * 0.9, top - 0.1, r * 0.8, "Slate", n=20, bands=6, ribs=6)
            frustum(m, sx_, sy_, 0.5, 0.5, top + r * 0.7, top + r * 0.7 + 1.0, 8, "LightWarm")
        for tx in (cx + 10.0, cx + 22.0):
            frustum(m, tx, y1 - 8.0, 4.6, 4.6, h + 0.2, h + 5.5, 24, "Metal", smooth=True)
            dome(m, tx, y1 - 8.0, 4.6, h + 5.4, 2.6, "Metal", n=24, bands=6)
        for px, py in ((x0 + 3.0, y1 - 3.0), (x1 - 3.0, y1 - 3.0)):
            pinnacle(m, px, py, 1.6, h + 0.2, h + 4.0, 3.0)
    m_current[0] = m
    inner_w = (x1 - x0) - 2 * WALL_T
    for k in range(4):
        bay(b, "S", inner_w * (k + 0.5) / 4.0, "Industry bay %d" % (k + 1), skills)
    interior_detail(b)


def b_guild(b, key, accent):
    m = M(key, "Districts")
    shell(m, b)
    x0, x1, y0, y1 = b["rect"]
    cx, cy = b["centre"]
    h = b["h_wall"]
    # A steep gabled roof along the long axis, twin entrance towers, and a small dome at the crossing.
    ridge = [(x0 + 1.0, y0 + 1.0, h + 0.2), (x0 + 1.0, y1 - 1.0, h + 0.2), (x0 + 1.0, cy, h + 9.0)]
    loft(m, ridge, [(x1 - 1.0, p[1], p[2]) for p in ridge], "Slate", top="Stone", bottom="Stone")
    door = b["doors"][0]
    face_y = y1 if door["side"] == "N" else y0
    out = 1.0 if door["side"] == "N" else -1.0
    for k in (-1, 1):
        tx = door["centre"][0] + k * 7.0
        ty = face_y - out * 2.8
        if accent == "round":
            round_tower(m, tx, ty, 3.0, h - 0.3, 23.0, 6.5, cap="pointed", n=16, slits=4)
        else:
            aabox(m, tx - 2.8, tx + 2.8, ty - 2.8, ty + 2.8, h - 0.3, 22.0, "Stone")
            aabox(m, tx - 3.05, tx + 3.05, ty - 3.05, ty + 3.05, 21.9, 22.5, "Brass")
            frustum(m, tx, ty, 3.6, 0.0, 22.4, 30.5, 4, "Slate", rot=math.pi / 4)
            frustum(m, tx, ty, 0.2, 0.0, 30.2, 32.4, 6, "Brass")
            r4 = (tx - 2.8, tx + 2.8, ty - 2.8, ty + 2.8)
            m_current[0] = m
            for side in ("S", "N", "E", "W"):
                lancet(r4, side, 2.8, 1.1, 14.0, 19.5, frame=False)
    drum_dome(m, cx, cy, 4.5, h + 7.0, 2.0, 4.5, "GlassDome", n=20, ribs=10, windows=8, lantern_r=0.9,
              spire_h=2.5)
    m_current[0] = m
    back = "S" if door["side"] == "N" else "N"
    inner_w = (x1 - x0) - 2 * WALL_T
    for k, label in enumerate(("Career giver 1", "Career giver 2", "Career giver 3")):
        counter(b, back, inner_w * (k + 0.5) / 3.0, 3.0, label, "careers",
                {"available": False, "note": "career chains are M8 and not designed (design-bible 4)"})
    interior_detail(b)


# --------------------------------------------------------------------------------------------------
# Docks
# --------------------------------------------------------------------------------------------------

def build_docks():
    for name, a in postern_axes():
        build_dock(name, a)


def build_dock(name, a):
    m = M("Dock_" + name, "Docks")
    m_current[0] = m
    w = M("Water", "Site")
    c, s = math.cos(a), math.sin(a)
    t = (-s, c)
    dcx, dcy = DOCK_C * math.copysign(1, c), DOCK_C * math.copysign(1, s)
    # The platform: its own floor hull, a deep skirt, paving on top.
    frustum(m, dcx, dcy, DOCK_R, DOCK_R, SKIRT_Z, FLOOR, 40, "StoneDark", top="Paving", collide="floor",
            rot=0.0)
    frustum(m, dcx, dcy, DOCK_R + 0.05, DOCK_R + 0.05, FLOOR - 0.6, FLOOR - 0.2, 40, "Brass")
    # The pad: dark, two lit rings, eight lights.
    px, py = dcx + c * PAD_OFF, dcy + s * PAD_OFF
    frustum(m, px, py, PAD_R, PAD_R, FLOOR - 0.01, FLOOR + LAYER["Pad"], 40, "Pad")
    annulus(m, PAD_R - 0.7, PAD_R - 0.3, FLOOR - 0.01, FLOOR + LAYER["PadRing"], "Light", n=40, cx=px, cy=py)
    annulus(m, 5.6, 6.0, FLOOR - 0.01, FLOOR + LAYER["PadRing"], "Light", n=32, cx=px, cy=py)
    for k in range(8):
        q = polar(PAD_R + 1.0, 2 * math.pi * k / 8, px, py)
        aabox(m, q[0] - 0.25, q[0] + 0.25, q[1] - 0.25, q[1] + 0.25, FLOOR - KNIT, FLOOR + 0.25, "Light",
              collide=True)
    # Light pylons on the rim either side of the pad -- not on the approach, which is straight outboard.
    for k in (-1, 1):
        q = polar(DOCK_R - 2.5, a + k * 1.05, dcx, dcy)
        aabox(m, q[0] - 0.6, q[0] + 0.6, q[1] - 0.6, q[1] + 0.6, FLOOR - KNIT, FLOOR + 9.0, "StoneDark",
              collide=True)
        aabox(m, q[0] - 0.7, q[0] + 0.7, q[1] - 0.7, q[1] + 0.7, FLOOR + 8.9, FLOOR + 9.3, "Brass")
        frustum(m, q[0], q[1], 0.45, 0.0, FLOOR + 9.2, FLOOR + 11.5, 6, "Light")
    # The hub: a rotunda with a door to the causeway and a door to the pad.
    hx, hy = dcx - c * HUB_OFF, dcy - s * HUB_OFF
    n = 24
    doors = [(a + math.pi, math.asin(2.4 / HUB_R)), (a, math.asin(2.4 / HUB_R))]
    def is_door(k):
        mid_k = 2 * math.pi * (k + 0.5) / n
        return any(ang_dist(mid_k, dc) < dh + math.pi / n * 0.5 for dc, dh in doors)

    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        mid = (a0 + a1) / 2
        if is_door(i):
            continue
        b0 = a0 + (0.01 if is_door(i - 1) else 0.0)
        b1 = a1 - (0.01 if is_door(i + 1) else 0.0)
        inward = (-math.cos(mid), -math.sin(mid))
        sector(m, HUB_R - 0.6, HUB_R, a0, a1, FLOOR - KNIT, 7.0, "Stone", cx=hx, cy=hy,
               side=lambda nn, w_=inward: "Interior" if nn[0] * w_[0] + nn[1] * w_[1] > 0.9 else "Stone",
               collide=True)
        sector(m, HUB_R - 0.05, HUB_R + 0.15, b0, b1, 3.0, 3.4, "Brass", cx=hx, cy=hy)
        if i % 2 == 0:
            p = polar(HUB_R - 0.03, mid, hx, hy)
            obox(m, p[0], p[1], 0.2, 1.2, 1.6, 5.4, mid, "Glass")
    for dc, dh in doors:
        sector(m, HUB_R - 0.6, HUB_R, dc - dh - 0.02, dc + dh + 0.02, 4.8, 7.0, "Stone", cx=hx, cy=hy)
        q = polar(HUB_R + 0.2, dc, hx, hy)
        obox(m, q[0], q[1], 0.4, 5.6, 4.6, 5.0, dc, "Brass")
    frustum(m, hx, hy, HUB_R - 0.55, HUB_R - 0.55, FLOOR - 0.01, FLOOR + LAYER["Floor"], 24, "Floor")
    frustum(m, hx, hy, HUB_R - 0.5, HUB_R - 0.5, 6.0, 6.4, 24, "Interior")
    frustum(m, hx, hy, 3.0, 3.0, 5.92, 6.05, 24, "Light")
    frustum(m, hx, hy, HUB_R + 0.2, HUB_R + 0.2, 6.9, 7.4, 24, "Brass", smooth=True)
    dome(m, hx, hy, HUB_R + 0.1, 7.3, 6.2, "GlassDome", n=24, bands=8, ribs=12)
    lantern(m, hx, hy, 13.0, 1.4, 2.0, 4.5)
    # The ships counter: a round desk in the middle of the hub, 5.9 m of floor all round.
    frustum(m, hx, hy, 1.5, 1.5, FLOOR - KNIT, FLOOR + 1.1, 24, "StoneDark", top="Brass", collide=True)
    frustum(m, hx, hy, 0.35, 0.35, FLOOR + 1.05, FLOOR + 2.6, 12, "Light")
    rec = {"key": "dock_" + name.lower(), "name": "Docking Station " + name}
    stand = (hx + c * 2.6, hy + s * 2.6)
    SERVICE_POINTS.append({"building": "Dock_" + name, "service": "ships", "label": "Ship services",
                           "stand_m": [round(stand[0], 3), round(stand[1], 3), FLOOR],
                           "facing_deg": round(math.degrees(a + math.pi), 2), "reach_m": 2.5})
    # short_name is what the flight readout calls it after the city's name: "Borlash NE dock" (task 169).
    BERTHS.append({"key": rec["key"], "name": rec["name"], "short_name": name + " dock",
                   "pad_m": [round(px, 3), round(py, 3), FLOOR],
                   "pad_radius_m": PAD_R, "dock_centre_m": [dcx, dcy, FLOOR], "dock_radius_m": DOCK_R,
                   "outward": [round(c, 6), round(s, 6)]})
    # Railing round the platform, open to the causeway and the two perimeter roads -- and narrower than
    # each opening's own railings by JOIN, so the two fences cross instead of leaving a slot between.
    lat = CAUSEWAY_HALF - RAIL_OFF - JOIN
    gaps = [(a + math.pi, math.asin(lat / DOCK_RAIL_R))]
    # The perimeter roads arrive along -x (or +x) and -y (or +y) of the dock centre; find where the
    # band between each road's two railings crosses the dock's railing circle.
    lo_off = (ROAD_IN + RAIL_OFF) - DOCK_C + JOIN
    hi_off = (PLAT - RAIL_OFF) - DOCK_C - JOIN
    for along_x in (True, False):
        angs = []
        for perp in (lo_off, hi_off):
            along = -math.sqrt(max(0.0, DOCK_RAIL_R ** 2 - perp ** 2))
            if along_x:
                vx, vy = along * math.copysign(1, c), perp * math.copysign(1, s)
            else:
                vx, vy = perp * math.copysign(1, c), along * math.copysign(1, s)
            angs.append(math.atan2(vy, vx))
        mid = math.atan2(math.sin(angs[0]) + math.sin(angs[1]), math.cos(angs[0]) + math.cos(angs[1]))
        gaps.append((mid, ang_dist(angs[0], angs[1]) / 2))
    for lo, hi in arc_gaps(gaps):
        rail_arc(M("Railings", "Props"), DOCK_RAIL_R, lo, hi, dcx, dcy, step_deg=8.0)
    TARGETS.append(("%s pad centre" % rec["name"], px, py))
    TARGETS.append(("%s hub" % rec["name"], hx - c * 4.5, hy - s * 4.5))
    m_current[0] = None


# --------------------------------------------------------------------------------------------------
# Build everything
# --------------------------------------------------------------------------------------------------

def build_city():
    reset()
    buildings = plan_buildings()
    BUILDINGS.extend(buildings)
    build_ground()
    build_wall()
    build_paving()
    build_fountain()
    by_key = dict((b["key"], b) for b in buildings)
    b_admin(by_key["Admin"])
    b_market(by_key["Market"])
    b_bank(by_key["BankNorth"], "BankNorth")
    b_bank(by_key["BankSouth"], "BankSouth")
    b_hq(by_key["HQ"])
    b_apartments(by_key["Apartments"])
    b_hotel(by_key["Hotel"])
    b_pub(by_key["Pub"])
    b_hall(by_key["CraftingHall"], "CraftingHall",
           ["toolcrafting", "shipcrafting", "electronics", "armorcrafting", "weaponcrafting", "construction"],
           "crafting")
    b_hall(by_key["RefiningHall"], "RefiningHall", ["refining"], "refining")
    b_guild(by_key["CraftingGuild"], "CraftingGuild", "square")
    b_guild(by_key["RefiningGuild"], "RefiningGuild", "round")
    build_docks()
    build_railings()
    # The closed buildings take what is left after the schedule, the parks and the streets, and the lamps
    # and trees then keep clear of them as they do of every other building.
    SCENERY.extend(plan_scenery(buildings))
    build_scenery(SCENERY)
    build_lamps(buildings + SCENERY)
    trees = build_parks_and_trees(buildings + SCENERY)
    # Places that must stay unreachable: water behind railings, and the inside of solid wall.
    NEGATIVES.append(("the lagoon, between the north bridge and the NE causeway", 95.0, 232.0))
    NEGATIVES.append(("the lagoon, by the west road", -250.0, 120.0))
    NEGATIVES.append(("the canal, east of north", 14.0, 36.5))
    NEGATIVES.append(("inside the city wall", 200.0 * math.cos(math.radians(20)), 200.0 * math.sin(math.radians(20))))
    for k in range(0, 360, 15):
        a = math.radians(k + 7.5)
        TARGETS.append(("ring road %d deg" % k, (RING_RI + 5.0) * math.cos(a), (RING_RI + 5.0) * math.sin(a)))
    for name, (x, y) in (("perimeter road N", (120.0, 267.0)), ("perimeter road S", (-120.0, -267.0)),
                         ("perimeter road E", (267.0, 140.0)), ("perimeter road W", (-267.0, -140.0))):
        TARGETS.append((name, x, y))
    return buildings, trees


# --------------------------------------------------------------------------------------------------
# Checks
# --------------------------------------------------------------------------------------------------

def check_water_over_ground(margin=0.05):
    """Every visible water surface stands above the ground under it, which is a sphere, not this plane.

    The canal's water was drawn 0.40 m below the paving where the levelled ground is only 0.03 m below
    it, so the terrain covered the water -- nothing in the drawing could show that, and nothing checked
    it until 2 October. Measured where each surface is nearest the centre, which is where the ground
    stands highest under it.
    """
    out = []
    for name, nearest, z in WATER_SURFACES:
        ground = -curvature_drop(nearest)
        clearance = z - ground
        print("  water: %-15s at %+.2f m, ground under its nearest edge %+.3f m, clear by %.3f m"
              % (name, z, ground, clearance))
        if clearance < margin:
            raise SystemExit("the %s's water is %.3f m above the ground under it; it needs %.2f m"
                             % (name, clearance, margin))
        out.append({"name": name, "surface_m": round(z, 3), "nearest_centre_m": round(nearest, 3),
                    "clearance_m": round(clearance, 3)})
    return out


def check_schedule(buildings):
    print("\n  schedule (A-07 station table)          built         source")
    bad = []
    for b in buildings:
        want = SCHEDULE_FOOTPRINTS[b["key"]]
        x0, x1, y0, y1 = b["rect"]
        got = (x1 - x0, y1 - y0)
        ok = abs(got[0] - want[0]) < 0.01 and abs(got[1] - want[1]) < 0.01
        print("  %-36s %5.1f x %4.1f   %3d x %2d  %s" % (b["name"][:36], got[0], got[1], want[0], want[1],
                                                       "ok" if ok else "MISMATCH"))
        if not ok:
            bad.append(b["key"])
    # Measured off the built geometry, not the constants: the wall's centreline, the docks' span.
    wall = MESHES[MESH_PREFIX + "Wall"]
    radii = [math.hypot(v[0], v[1]) for v in wall.v if abs(v[2] - WALL_TOP) < 1e-6]
    ri = min(r for r in radii if r > 190)
    ro = max(r for r in radii if r < 205)
    centre_line = (ri + ro) / 2
    docks_x = [v[0] for name, md in MESHES.items() if name.startswith(MESH_PREFIX + "Dock_") for v in md.v
               if abs(v[2] - SKIRT_Z) < 1e-6]
    span = max(docks_x) - min(docks_x)
    counts = {
        "docks": len(BERTHS), "gates": len(GATES),
        "market terminals": sum(1 for s in SERVICE_POINTS if s["service"] == "market"),
        "industry bays, crafting": sum(1 for s in SERVICE_POINTS if s["building"] == "CraftingHall"),
        "industry bays, refining": sum(1 for s in SERVICE_POINTS if s["building"] == "RefiningHall"),
    }
    expected = {"docks": 4, "gates": 4, "market terminals": 6, "industry bays, crafting": 4,
                "industry bays, refining": 4}
    print("  %-36s %9.2f   %9.2f  %s" % ("wall centreline radius (m)", centre_line, 200.0,
                                         "ok" if abs(centre_line - 200.0) < 0.05 else "MISMATCH"))
    print("  %-36s %9.2f   %9.2f  %s" % ("across the docks (m)", span, 584.0,
                                         "ok" if abs(span - 584.0) < 0.2 else "MISMATCH"))
    if abs(centre_line - 200.0) >= 0.05:
        bad.append("wall radius")
    if abs(span - 584.0) >= 0.2:
        bad.append("dock span")
    for k, want in expected.items():
        ok = counts[k] == want
        print("  %-36s %9d   %9d  %s" % (k, counts[k], want, "ok" if ok else "MISMATCH"))
        if not ok:
            bad.append(k)
    hq_top = max(v[2] for v in MESHES[MESH_PREFIX + "HQ"].v)
    print("  %-36s %9.1f   %9s  %s" % ("HQ top (m), A-07 asks at least 45", hq_top, ">= 45",
                                       "ok" if hq_top >= 45.0 else "TOO LOW"))
    if hq_top < 45.0:
        bad.append("HQ height")
    if bad:
        raise SystemExit("model disagrees with the source: " + ", ".join(bad))


def _plane_basis(n):
    ref = (0.0, 0.0, 1.0) if abs(n[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = norm(cross(n, ref))
    v = cross(n, u)
    return u, v


def _clip(subject, clip):
    """Sutherland-Hodgman: intersection of two convex 2D polygons (both CCW)."""
    out = subject
    for i in range(len(clip)):
        a, b = clip[i], clip[(i + 1) % len(clip)]
        inp, out = out, []
        if not inp:
            break

        def inside(p):
            return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) >= -1e-9

        for j in range(len(inp)):
            p, q = inp[j], inp[(j + 1) % len(inp)]
            pi, qi = inside(p), inside(q)
            if pi:
                out.append(p)
            if pi != qi:
                dx, dy = q[0] - p[0], q[1] - p[1]
                ex, ey = b[0] - a[0], b[1] - a[1]
                den = ex * dy - ey * dx
                if abs(den) > 1e-15:
                    tpar = (ey * (p[0] - a[0]) - ex * (p[1] - a[1])) / den
                    out.append((p[0] + dx * tpar, p[1] + dy * tpar))
    return out


def _area2(poly):
    return sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
               for i in range(len(poly))) / 2.0


def _inside_any(p, exclude):
    for k, (bb, planes) in enumerate(SOLIDS):
        if k in exclude or not planes:
            continue
        if not (bb[0] - 1e-6 <= p[0] <= bb[1] + 1e-6 and bb[2] - 1e-6 <= p[1] <= bb[3] + 1e-6 and
                bb[4] - 1e-6 <= p[2] <= bb[5] + 1e-6):
            continue
        if all(dot(nrm, p) < d - 1e-5 for nrm, d in planes):
            return True
    return False


def _sealed(points, normal, exclude):
    """Is a patch covered on its outward side everywhere we look?

    Probed 2 mm outside the faces at the patch's centre and towards each of its corners. One probe at
    the centre is not enough: a wall built of segments puts that centre exactly on the plane where two
    segments meet, which is strictly inside neither, and a buried face reads as visible.
    """
    cen = centroid(points)
    samples = [cen] + [add(mul(p, 0.7), mul(cen, 0.3)) for p in points[:8]]
    # A millimetre of sideways jitter as well, for the same reason: a centre on the axis of a ring of
    # sectors sits exactly on the boundary between two of them.
    u, v = _plane_basis(normal)
    jitter = add(mul(u, 0.0013), mul(v, 0.0007))
    return all(_inside_any(add(add(q, jitter), mul(normal, 0.002)), exclude) for q in samples)


def report_coincident_faces(limit=40):
    """Same-facing coplanar faces of different solids, graded the way the skill grades them.

    Generalised from greybox_lib's axis-aligned version to any planar face: group faces by plane,
    intersect pairs in that plane, then ask whether a third solid seals the overlap and whether the
    two materials differ. Only overlaps across two materials that nothing seals can flicker.
    """
    groups = {}
    for md in MESHES.values():
        for fi, idx in enumerate(md.f):
            poly = [md.v[k] for k in idx]
            nrm = newell(poly)
            area = math.sqrt(dot(nrm, nrm)) / 2.0
            if area < 1e-6:
                continue
            n = mul(nrm, 0.5 / area)
            d = dot(n, poly[0])
            key = (round(n[0], 3), round(n[1], 3), round(n[2], 3), round(d, 3))
            groups.setdefault(key, []).append((md.name, md.fm[fi], md.fsolid[fi], poly, n))
    found, buried, same = [], 0, 0
    for key, faces in groups.items():
        if len(faces) < 2:
            continue
        n = faces[0][4]
        u, v = _plane_basis(n)
        prepared = []
        for name, mat, sid, poly, _n in faces:
            p2 = [(dot(p, u), dot(p, v)) for p in poly]
            # The key rounds the normal, which groups planes through the origin that are a fraction of a
            # degree apart -- 8 cm apart at the wall. Keep each face's exact plane to test pairs with.
            if _area2(p2) < 0:
                p2 = list(reversed(p2))
            bb = (min(p[0] for p in p2), max(p[0] for p in p2), min(p[1] for p in p2), max(p[1] for p in p2))
            prepared.append((name, mat, sid, p2, bb, poly, _n, dot(_n, poly[0])))
        prepared.sort(key=lambda r: r[4][0])
        for i in range(len(prepared)):
            ni, mi, si, pi, bi, qi, nni, di = prepared[i]
            for j in range(i + 1, len(prepared)):
                nj, mj, sj, pj, bj, qj, nnj, dj = prepared[j]
                if bj[0] > bi[1] - 1e-6:
                    break
                if si == sj and si >= 0:
                    continue
                if bj[2] > bi[3] - 1e-6 or bi[2] > bj[3] - 1e-6:
                    continue
                if max(abs(dot(nni, q) - di) for q in qj) > 1e-3 or max(abs(dot(nnj, q) - dj) for q in qi) > 1e-3:
                    continue
                inter = _clip(pi, pj)
                if len(inter) < 3:
                    continue
                area = abs(_area2(inter))
                if area < 1e-3:
                    continue
                if mi == mj:
                    same += 1
                    continue
                pts3 = [add(mul(n, key[3]), add(mul(u, a_), mul(v, b_))) for a_, b_ in inter]
                if _sealed(pts3, n, (si, sj)):
                    buried += 1
                    continue
                found.append((area, centroid(pts3), ni, mi, nj, mj))
    found.sort(key=lambda f: -f[0])
    print("\n  coincident, sealed inside a third solid: %d (cannot be seen)" % buried)
    print("  coincident, same material both sides:    %d (ties, no flicker)" % same)
    if not found:
        print("  coincident across two materials:         none")
        return 0
    print("  coincident across two materials:         %d, worst first" % len(found))
    for area, c3, ni, mi, nj, mj in found[:limit]:  # noqa
        print("    %8.3f m2 at (%.2f, %.2f, %.2f)  %s/%s vs %s/%s" % (area, c3[0], c3[1], c3[2], ni, mi, nj, mj))
    return len(found)


def _convex2d(points):
    pts = sorted(set((round(p[0], 6), round(p[1], 6)) for p in points))
    if len(pts) < 3:
        return pts

    def cr(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lo, hi = [], []
    for p in pts:
        while len(lo) > 1 and cr(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(hi) > 1 and cr(hi[-2], hi[-1], p) <= 0:
            hi.pop()
        hi.append(p)
    return lo[:-1] + hi[:-1]


def route_check(cell=0.5):
    """Flood the walking level on a grid rasterised from the BUILT collision hulls.

    Support is every floor hull; obstacles are hulls that reach into the band a standing character
    occupies, grown by half the 1.2 m route minimum plus half a cell diagonal -- conservative, so a
    reachable cell has at least the minimum route width around it. Everything named in TARGETS must
    be reached and nothing in NEGATIVES may be, which is what proves the railings seal the water.
    """
    lo_b, hi_b = FLOOR + 0.05, FLOOR + PAWN_H
    pad = ROUTE_MIN / 2.0 + cell * math.sqrt(0.5)
    extent = PLAT + RAMP_LEN + 8.0
    n = int(2 * extent / cell) + 1

    def idx(x, y):
        return int(round((x + extent) / cell)), int(round((y + extent) / cell))

    support = bytearray(n * n)
    blocked = bytearray(n * n)
    floors, blocks = [], []
    for md in MESHES.values():
        for hull in md.hulls:
            zs = [p[2] for p in hull["verts"]]
            poly = _convex2d([(p[0], p[1]) for p in hull["verts"]])
            if hull["kind"] == "floor":
                floors.append(poly)
            elif max(zs) > lo_b and min(zs) < hi_b:
                blocks.append(poly)

    def raster(poly, target, margin):
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        i0, j0 = idx(min(xs) - abs(margin), min(ys) - abs(margin))
        i1, j1 = idx(max(xs) + abs(margin), max(ys) + abs(margin))
        edges = []
        for k in range(len(poly)):
            a, b = poly[k], poly[(k + 1) % len(poly)]
            L = math.hypot(b[0] - a[0], b[1] - a[1])
            if L < 1e-9:
                continue
            edges.append((a, (b[0] - a[0]) / L, (b[1] - a[1]) / L))
        for i in range(max(0, i0), min(n, i1 + 1)):
            x = i * cell - extent
            for j in range(max(0, j0), min(n, j1 + 1)):
                y = j * cell - extent
                ok = True
                for a, ex, ey in edges:
                    if ex * (y - a[1]) - ey * (x - a[0]) < -margin:
                        ok = False
                        break
                if ok:
                    target[i * n + j] = 1

    for poly in floors:
        raster(poly, support, -cell * 0.5)
    for poly in blocks:
        raster(poly, blocked, pad)
    start = idx(0.0, -24.0)
    s0 = start[0] * n + start[1]
    if not support[s0] or blocked[s0]:
        raise SystemExit("the route check's start point on the square is not walkable")
    dist = {s0: 0}
    q = deque([s0])
    while q:
        k = q.popleft()
        i, j = divmod(k, n)
        dk = dist[k] + 1
        for ii, jj in ((i - 1, j), (i + 1, j), (i, j - 1), (i, j + 1)):
            if 0 <= ii < n and 0 <= jj < n:
                kk = ii * n + jj
                if kk not in dist and support[kk] and not blocked[kk]:
                    dist[kk] = dk
                    q.append(kk)
    missing, reached = [], {}
    for name, x, y in TARGETS:
        i, j = idx(x, y)
        best = None
        for di in (0, -1, 1, -2, 2):
            for dj in (0, -1, 1, -2, 2):
                kk = (i + di) * n + (j + dj)
                if kk in dist and (best is None or dist[kk] < best):
                    best = dist[kk]
        if best is None:
            missing.append((name, x, y))
        else:
            reached[name] = best * cell
    leaks = []
    for name, x, y in NEGATIVES:
        i, j = idx(x, y)
        if (i * n + j) in dist:
            leaks.append((name, x, y))
    print("\n  route check: %d reachable cells of %.1f m from the square; %d hulls block, %d floors"
          % (len(dist), cell, len(blocks), len(floors)))
    if missing:
        for name, x, y in missing[:20]:
            print("    UNREACHABLE  %s at (%.1f, %.1f)" % (name, x, y))
    if leaks:
        for name, x, y in leaks:
            print("    LEAK         %s at (%.1f, %.1f) is reachable and must not be" % (name, x, y))
    for name in sorted(reached):
        if name.endswith("pad centre") or name.endswith("gate, outside") or name.startswith("ramp foot"):
            print("    %-46s %6.0f m by the grid" % (name, reached[name]))
    if missing or leaks:
        raise SystemExit("route check failed: %d unreachable, %d leaks" % (len(missing), len(leaks)))
    print("    all %d targets reached; %d negative controls unreachable" % (len(TARGETS), len(NEGATIVES)))
    return {"cell_m": cell, "reachable_cells": len(dist), "targets": len(TARGETS),
            "negatives": [n_[0] for n_ in NEGATIVES],
            "routes_m": dict((k, round(v, 1)) for k, v in reached.items()
                             if k.endswith("pad centre") or k.startswith("ramp foot") or "gate" in k)}


def report_bounds():
    lo, hi = [1e9] * 3, [-1e9] * 3
    faces = 0
    for md in MESHES.values():
        faces += len(md.f)
        for v in md.v:
            for i in range(3):
                lo[i] = min(lo[i], v[i])
                hi[i] = max(hi[i], v[i])
    print("")
    for i, ax in enumerate("XYZ"):
        print("  bounds %s %8.2f .. %8.2f  (%.2f m)" % (ax, lo[i], hi[i], hi[i] - lo[i]))
    hulls = sum(len(md.hulls) for md in MESHES.values())
    print("  meshes %d, faces %d, collision hulls %d" % (len(MESHES), faces, hulls))
    # The skirt has to stay under the levelled ground everywhere it reaches.
    far = max(math.hypot(v[0], v[1]) for md in MESHES.values() for v in md.v if v[2] <= SKIRT_Z + 0.01)
    drop = curvature_drop(far)
    print("  skirt bottom %.1f m; the levelled sphere is %.2f m below the plane at its farthest corner (%.0f m)"
          % (SKIRT_Z, drop, far))
    if SKIRT_Z > -drop - 1.0:
        raise SystemExit("the skirt would show above the levelled ground at the corners")
    return lo, hi, faces, hulls


def check_names():
    names = set(MESHES)
    mats = set(MAT_PREFIX + k for k in MATERIALS)
    clash = names & mats
    if clash:
        raise SystemExit("mesh and material names collide: %s" % ", ".join(sorted(clash)))
    for md in MESHES.values():
        for mat in set(md.fm):
            if mat not in MATERIALS:
                raise SystemExit("%s uses undeclared material %s" % (md.name, mat))


# --------------------------------------------------------------------------------------------------
# Blender
# --------------------------------------------------------------------------------------------------

def clear_scene():
    scene = bpy.context.scene
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for coll in list(bpy.data.collections):
        bpy.data.collections.remove(coll)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.cameras, bpy.data.lights):
        for item in list(block):
            block.remove(item)
    for world in list(bpy.data.worlds):
        if world.users == 0 or world.name.startswith("Borlash"):
            bpy.data.worlds.remove(world)
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"


def make_materials():
    made = {}
    for name, (rgb_s, metal, rough, emit_s, strength) in MATERIALS.items():
        rgb = srgb(rgb_s)
        emit = srgb(emit_s) if emit_s else None
        mat = bpy.data.materials.new(MAT_PREFIX + name)
        mat.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
        mat.metallic = metal
        mat.roughness = rough
        bsdf = mat.node_tree.nodes.get("Principled BSDF") if mat.node_tree else None
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
            bsdf.inputs["Metallic"].default_value = metal
            bsdf.inputs["Roughness"].default_value = rough
            if emit:
                bsdf.inputs["Emission Color"].default_value = (emit[0], emit[1], emit[2], 1.0)
                bsdf.inputs["Emission Strength"].default_value = strength
        made[name] = mat
    return made


def realize():
    clear_scene()
    scene = bpy.context.scene
    mats = make_materials()
    root = bpy.data.collections.new("BorlashCity")
    scene.collection.children.link(root)
    colls = {}
    objects = {}
    for name in sorted(MESHES):
        md = MESHES[name]
        if md.coll not in colls:
            c = bpy.data.collections.new(md.coll)
            root.children.link(c)
            colls[md.coll] = c
        used = sorted(set(md.fm))
        index = dict((mname, i) for i, mname in enumerate(used))
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(md.v, [], md.f)
        for mname in used:
            mesh.materials.append(mats[mname])
        mesh.polygons.foreach_set("material_index", [index[x] for x in md.fm])
        mesh.polygons.foreach_set("use_smooth", [bool(x) for x in md.fs])
        mesh.update()
        if len(mesh.polygons) != len(md.f):
            raise SystemExit("%s lost faces on the way into Blender" % name)
        obj = bpy.data.objects.new(name, mesh)
        colls[md.coll].objects.link(obj)
        objects[name] = obj
    ucx = bpy.data.collections.new("Collision")
    root.children.link(ucx)
    hull_objects = []
    for name in sorted(MESHES):
        for i, hull in enumerate(MESHES[name].hulls):
            hname = "UCX_%s_%03d" % (name, i)
            mesh = bpy.data.meshes.new(hname)
            mesh.from_pydata(hull["verts"], [], hull["faces"])
            mesh.update()
            obj = bpy.data.objects.new(hname, mesh)
            obj.display_type = "WIRE"
            obj.hide_render = True
            ucx.objects.link(obj)
            hull_objects.append(obj)
    ucx.hide_viewport = False
    ucx.hide_render = True
    markers = bpy.data.collections.new("Markers")
    root.children.link(markers)
    for b in BERTHS:
        e = bpy.data.objects.new("BERTH_" + b["key"], None)
        e.location = b["pad_m"]
        e.empty_display_type = "CIRCLE"
        e.empty_display_size = b["pad_radius_m"]
        markers.objects.link(e)
    for k, s in enumerate(SERVICE_POINTS):
        e = bpy.data.objects.new("SERVICE_%02d_%s_%s" % (k, s["building"], s["service"]), None)
        e.location = s["stand_m"]
        e.empty_display_type = "SINGLE_ARROW"
        e.empty_display_size = 1.5
        e.rotation_euler = (math.radians(90), 0.0, math.radians(s["facing_deg"] - 90))
        markers.objects.link(e)
    return objects, hull_objects, colls, root


# --------------------------------------------------------------------------------------------------
# Previews
# --------------------------------------------------------------------------------------------------

def preview_scene():
    scene = bpy.context.scene
    coll = bpy.data.collections.get("Preview") or bpy.data.collections.new("Preview")
    if coll.name not in scene.collection.children:
        scene.collection.children.link(coll)
    # Ground beyond the platform, roughly where the levelled terrain is. Not exported.
    mesh = bpy.data.meshes.new("Preview_Terrain")
    ring = ngon(0, 0, 2400.0, 64)
    mesh.from_pydata(at_z(ring, -1.8), [], [tuple(range(64))])
    mesh.update()
    mat = bpy.data.materials.new("Preview_TerrainMat")
    mat.diffuse_color = (0.16, 0.19, 0.15, 1.0)
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (0.16, 0.19, 0.15, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.95
    mesh.materials.append(mat)
    terrain = bpy.data.objects.new("Preview_Terrain", mesh)
    coll.objects.link(terrain)
    world = bpy.data.worlds.new("Borlash_Sky")
    world.use_nodes = True
    nt = world.node_tree
    bg = nt.nodes.get("Background")
    try:
        sky = nt.nodes.new("ShaderNodeTexSky")
        sky.sky_type = "MULTIPLE_SCATTERING"
        sky.sun_elevation = math.radians(32.0)
        sky.sun_rotation = math.radians(140.0)
        nt.links.new(sky.outputs["Color"], bg.inputs["Color"])
        bg.inputs["Strength"].default_value = 0.25
    except Exception:
        bg.inputs["Color"].default_value = (0.45, 0.6, 0.8, 1.0)
        bg.inputs["Strength"].default_value = 0.8
    scene.world = world
    sun_data = bpy.data.lights.new("Preview_Sun", type="SUN")
    sun_data.energy = 5.0
    sun_data.angle = math.radians(1.2)
    sun = bpy.data.objects.new("Preview_Sun", sun_data)
    sun.rotation_euler = (math.radians(58.0), 0.0, math.radians(140.0))
    coll.objects.link(sun)
    scene.render.engine = "BLENDER_EEVEE"
    try:
        scene.view_settings.view_transform = "AgX"
    except Exception:
        pass
    try:
        scene.view_settings.exposure = -0.2
    except Exception:
        pass
    for attr in ("use_raytracing", "use_shadows"):
        if hasattr(scene.eevee, attr):
            setattr(scene.eevee, attr, True)
    scene.eevee.taa_render_samples = 32
    return coll


def camera(coll, name, loc, target, lens=35.0, ortho=None, clip_end=6000.0):
    data = bpy.data.cameras.new(name)
    obj = bpy.data.objects.new(name, data)
    coll.objects.link(obj)
    obj.location = loc
    d = Vector(target) - Vector(loc)
    obj.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    data.lens = lens
    data.clip_start = 0.2
    data.clip_end = clip_end
    if ortho:
        data.type = "ORTHO"
        data.ortho_scale = ortho
    return obj


SHOTS = {
    # name: (location, target, lens, ortho, resolution)
    "Borlash_Overview": ((420.0, -560.0, 380.0), (0.0, 10.0, 0.0), 32.0, None, (1920, 1200)),
    "Borlash_Plan": ((0.0, 0.0, 1400.0), (0.0, 0.0, 0.0), 35.0, 640.0, (1600, 1600)),
    "Borlash_Approach": ((520.0, 470.0, 70.0), (180.0, 150.0, 8.0), 28.0, None, (1920, 1080)),
    "Borlash_SouthGate": ((0.0, -232.0, FLOOR + 1.7), (0.0, -150.0, 9.0), 22.0, None, (1920, 1080)),
    "Borlash_Square": ((-22.0, -24.0, FLOOR + 1.7), (12.0, 60.0, 16.0), 20.0, None, (1920, 1080)),
    "Borlash_MarketInside": ((-86.0, 82.6, FLOOR + 1.7), (-86.0, 120.0, 3.0), 16.0, None, (1920, 1080)),
    "Borlash_Dock": ((272.0, 272.0, FLOOR + 1.7), (60.0, 60.0, 20.0), 20.0, None, (1920, 1080)),
}


def render_shot(out, name):
    scene = bpy.context.scene
    coll = bpy.data.collections.get("Preview")
    loc, target, lens, ortho, res = SHOTS[name]
    cam = bpy.data.objects.get(name) or camera(coll, name, loc, target, lens, ortho)
    scene.camera = cam
    extra = None
    if name == "Borlash_MarketInside":
        data = bpy.data.lights.new("Preview_MarketFill", type="AREA")
        data.energy = 9000.0
        data.size, data.size_y = 50.0, 30.0
        data.shape = "RECTANGLE"
        extra = bpy.data.objects.new("Preview_MarketFill", data)
        extra.location = (-86.0, 100.0, H_HALL - 0.4)
        extra.visible_camera = False
        coll.objects.link(extra)
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = os.path.join(out, name + ".png")
    bpy.ops.render.render(write_still=True)
    if extra is not None:
        bpy.data.objects.remove(extra, do_unlink=True)
    print("  wrote %s" % scene.render.filepath)
    return scene.render.filepath


# --------------------------------------------------------------------------------------------------
# Export
# --------------------------------------------------------------------------------------------------

def export_engine(engine_dir, objects, hulls, stats):
    os.makedirs(engine_dir, exist_ok=True)
    fbx = os.path.join(engine_dir, "Borlash.fbx")
    for o in bpy.context.scene.objects:
        o.select_set(False)
    for o in list(objects.values()) + hulls:
        o.select_set(True)
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Z", axis_up="Y",
                             mesh_smooth_type="FACE", bake_space_transform=False, use_mesh_modifiers=False,
                             bake_anim=False)
    for o in bpy.context.scene.objects:
        o.select_set(False)
    print("  wrote %s" % fbx)
    meshes = []
    for name in sorted(MESHES):
        md = MESHES[name]
        xs = [v[0] for v in md.v]
        ys = [v[1] for v in md.v]
        zs = [v[2] for v in md.v]
        meshes.append({"name": name, "materials": [MAT_PREFIX + m for m in sorted(set(md.fm))],
                       "faces": len(md.f), "hulls": len(md.hulls),
                       "bounds_m": [[min(xs), max(xs)], [min(ys), max(ys)], [min(zs), max(zs)]]})
    manifest = {
        "asset": "Borlash City / A-07, round, from the concept art",
        "task": 167,
        "built": time.strftime("%Y-%m-%d %H:%M"),
        "generator": "tools/greybox/a07_borlash_city.py",
        "units": "Blender metres, +X east, +Y north, +Z up. The FBX importer turns these into Unreal "
                 "centimetres as (x*100, -y*100, z*100): Unreal is left-handed, so north is -Y there.",
        "origin": "the levelled ground at the city centre; the walking surface is floor_m above it",
        "floor_m": FLOOR,
        "drawn_planet_radius_m": R_DRAWN,
        "pad_required": {"flat_radius_m": 420.0, "blend_m": 150.0,
                         "why": "the platform's farthest corner is %.0f m out; the pad must be flat past it"
                                % stats["far"]},
        "skirt_bottom_m": SKIRT_Z,
        "airspace": {"no_fly_radius_m": 215.0, "no_fly_ceiling_m": 3000.0,
                     "why": "the wall, turrets and gate towers reach %.1f m from the centre" % stats["wall_reach"],
                     # No exception at the docks: a pilot there docks (G) rather than leaving the ship
                     # standing on one of the four berths there are (task 169).
                     "no_disembark": {"square_half_m": PLAT,
                                      "why": "the whole platform, docks included: at a dock you dock"}},
        "materials": dict((MAT_PREFIX + k, {"base_colour_linear": [round(c, 5) for c in srgb(v[0])],
                                            "base_colour_srgb": list(v[0]), "metallic": v[1],
                                            "roughness": v[2],
                                            "emission_linear": [round(c, 5) for c in srgb(v[3])] if v[3] else None,
                                            "emission_strength": v[4]})
                          for k, v in MATERIALS.items()),
        "meshes": meshes,
        "berths": BERTHS,
        "service_points": SERVICE_POINTS,
        "doors": DOORS,
        "gates": GATES,
        # Water levels against the city's plane, and how far each clears the levelled ground (a sphere)
        # where it comes nearest the centre. SpaceMMO.Terrain.CityWaterStandsAboveTheGround checks these
        # against the game's own terrain.
        "water": stats["water"],
        # Closed buildings: solid, collided, not enterable (Joe, 2 October: "scenery we can't enter for now").
        "scenery": [{"rect_m": [round(c, 3) for c in b["rect"]], "height_m": b["height"], "roof": b["roof"]}
                    for b in SCENERY],
        "buildings": [{"key": b["key"], "name": b["name"], "role": b["role"],
                       "rect_m": [round(c, 3) for c in b["rect"]], "wall_height_m": b["h_wall"],
                       "services": sorted(set(b["services"])), "note": b["note"]} for b in BUILDINGS],
        "checks": stats["checks"],
        "deviations": DEVIATIONS,
    }
    path = os.path.join(engine_dir, "Borlash_manifest.json")
    with open(path, "w") as fh:
        json.dump(manifest, fh, indent=1)
    print("  wrote %s" % path)
    return fbx, path


DEVIATIONS = [
    "Round wall, after the concept art and Joe's sketch; A-07 drew a clipped square (task 154 kept it).",
    "The city stands in a square lagoon on a platform, after the concept's city on water; A-07 has none.",
    "HQ central spire reaches 62 m (A-07: 45 m). The concept's spires dominate the skyline; the plan's "
    "own visibility arithmetic only gets better with height.",
    "Town square made round, r 35 m inside a 6 m canal ring (A-07: 48 x 48 m square, no canal).",
    "Admin, Market, HQ, Apartments, Hotel, Pub, guild halls and the south bank moved inward from A-07's "
    "positions where the round wall cut a corner the square wall did not; each move is on its row.",
    "Docks are round platforms of A-07's 584 m span, not 64 x 40 m rectangles.",
    "Every building has a walk-in ground-floor hall with the plan set's kit of parts; A-07 drew no "
    "interiors. Upper floors are closed.",
    "No collision on roofs, domes or spires: nothing can climb, and ships may not fly over the city.",
    "The districts are filled with closed buildings that cannot be entered -- the concept is denser than "
    "A-07's twelve, and Joe chose scenery for the gaps on 2 October. Each is one solid hull.",
    "The canal's water stands 0.20 m below the paving, not with the lagoon's at 0.70: the levelled ground "
    "is a sphere, 3 cm below the plane at the canal, and covered the lower water.",
]


def verify_roundtrip(fbx, objects, hulls):
    """Re-import the FBX into a scratch scene and compare names, counts and bounds."""
    live = bpy.context.window.scene if bpy.context.window else bpy.context.scene
    scratch = bpy.data.scenes.new("Borlash_Roundtrip")
    if bpy.context.window:
        bpy.context.window.scene = scratch
    # Object names are global to a .blend, so in a live session the import would come back suffixed
    # .001 beside the originals. Step the originals aside for the duration, and put them back after.
    originals = list(objects.values()) + list(hulls)
    expect_hulls = set(h.name for h in hulls)
    for o in originals:
        o.name = o.name + "~live"
    before = set(bpy.data.objects)
    mats_before = set(bpy.data.materials)
    bpy.ops.import_scene.fbx(filepath=fbx)
    imported = [o for o in bpy.data.objects if o not in before]
    try:
        names = dict((o.name, o) for o in imported if o.type == "MESH")
        expect_render = set(MESHES)
        got_render = set(n for n in names if not n.startswith("UCX_"))
        got_hulls = set(n for n in names if n.startswith("UCX_"))
        if got_render != expect_render:
            raise SystemExit("FBX roundtrip: render meshes differ: %s" % sorted(got_render ^ expect_render)[:8])
        if got_hulls != expect_hulls:
            raise SystemExit("FBX roundtrip: %d hulls back, %d sent" % (len(got_hulls), len(expect_hulls)))
        worst = 0.0
        for n in got_render:
            o = names[n]
            vs = [o.matrix_world @ v.co for v in o.data.vertices]
            md = MESHES[n]
            for axis in range(3):
                worst = max(worst, abs(min(v[axis] for v in vs) - min(p[axis] for p in md.v)),
                            abs(max(v[axis] for v in vs) - max(p[axis] for p in md.v)))
        if worst > 0.002:
            raise SystemExit("FBX roundtrip: bounds moved by %.4f m" % worst)
        print("  FBX roundtrip: %d meshes and %d hulls back, bounds within %.5f m" %
              (len(got_render), len(got_hulls), worst))
        return {"render_meshes": len(got_render), "hulls": len(got_hulls), "max_bounds_error_m": worst}
    finally:
        for o in imported:
            data = o.data
            bpy.data.objects.remove(o, do_unlink=True)
            if data is not None and data.users == 0 and isinstance(data, bpy.types.Mesh):
                bpy.data.meshes.remove(data)
        for mat in list(bpy.data.materials):
            if mat not in mats_before and mat.users == 0:
                bpy.data.materials.remove(mat)
        for o in originals:
            o.name = o.name[:-len("~live")]
        if bpy.context.window:
            bpy.context.window.scene = live
        bpy.data.scenes.remove(scratch)


# --------------------------------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------------------------------

def parse(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []

    def flag(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default

    return {"out": os.path.abspath(flag("--out", DEFAULT_OUT)),
            "engine": os.path.abspath(flag("--engine", DEFAULT_ENGINE)),
            "check_only": "--check-only" in argv,
            "render": "--render" in argv,
            "shots": flag("--shots", "")}


def main(argv=None):
    args = parse(list(argv if argv is not None else sys.argv))
    if os.path.abspath(args["out"]).startswith(os.path.abspath(ROOT)):
        raise SystemExit("working files belong outside the repository; only the engine files go in it")
    t0 = time.time()
    buildings, trees = build_city()
    check_names()
    check_schedule(buildings)
    lo, hi, faces, hulls = report_bounds()
    water = check_water_over_ground()
    flicker = report_coincident_faces()
    routes = route_check()
    far = max(math.hypot(v[0], v[1]) for md in MESHES.values() for v in md.v)
    wall_reach = max(math.hypot(v[0], v[1]) for name in (MESH_PREFIX + "Wall",) for v in MESHES[name].v
                     if v[2] > FLOOR + 2.0)
    print("\n  trees %d, closed buildings %d, service points %d, berths %d, doors %d, built in %.1f s"
          % (len(trees), len(SCENERY), len(SERVICE_POINTS), len(BERTHS), len(DOORS), time.time() - t0))
    if flicker:
        raise SystemExit("%d coincident faces across two materials" % flicker)
    stats = {"far": far, "wall_reach": wall_reach,
             "water": water,
             "checks": {"schedule": "PASS", "coincident_across_materials": flicker, "routes": routes,
                        "closed_buildings": len(SCENERY), "water_over_ground": "PASS",
                        "faces": faces, "hulls": hulls,
                        "bounds_m": [[round(lo[i], 3), round(hi[i], 3)] for i in range(3)]}}
    if args["check_only"]:
        print("\nPASS checks only; nothing written")
        return stats
    objects, hull_objs, colls, root = realize()
    os.makedirs(args["out"], exist_ok=True)
    fbx, manifest = export_engine(args["engine"], objects, hull_objs, stats)
    stats["checks"]["fbx_roundtrip"] = verify_roundtrip(fbx, objects, hull_objs)
    with open(manifest) as fh:
        data = json.load(fh)
    data["checks"] = stats["checks"]
    with open(manifest, "w") as fh:
        json.dump(data, fh, indent=1)
    colls["Collision"] = bpy.data.collections.get("Collision")
    if colls["Collision"] is not None:
        colls["Collision"].hide_viewport = True
    preview_scene()
    blend = os.path.join(args["out"], "BorlashCity.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    print("  wrote %s" % blend)
    if args["render"]:
        shots = [s for s in args["shots"].split(",") if s] or list(SHOTS)
        for name in shots:
            render_shot(args["out"], name)
    print("\nPASS Borlash built, checked, exported in %.1f s" % (time.time() - t0))
    return stats


if __name__ == "__main__":
    main()
