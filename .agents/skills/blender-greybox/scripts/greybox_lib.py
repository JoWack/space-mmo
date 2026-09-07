#!/usr/bin/env python
"""
Shared machinery for greybox build scripts. Import it; do not edit it per model.

    import os, sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from greybox_lib import *

A model script declares GROUPS and MATERIALS, states its geometry as solids,
and calls the checks. Everything below is the part that is the same whether
you are building a station, a hangar, a ship or a character proxy -- and it is
the part that took a real project several playtests to get right, so it is
worth reading the comments before working around any of it.

Coordinates. Work in "plan" coordinates: x and y read straight off the source
drawing, z up from the floor. set_extent() tells the library where the origin
belongs, and to_local() maps plan -> Blender with +Y north. This keeps the
numbers in the build script identical to the numbers on the drawing, which is
what makes a transcription auditable.
"""

import math
import os
import sys

import bpy
from mathutils import Vector

# --------------------------------------------------------------------------
# State
# --------------------------------------------------------------------------

GROUPS = {}       # mesh group name -> (collection name, material name)
MATERIALS = {}    # material name -> (r, g, b, a)

_geometry = {}
_boxes = []       # (group, label, (x0, x1, y0, y1, z0, z1)) in plan metres
_extent = (0.0, 0.0)
AXIS_NAME = ("x", "y", "z")


def configure(groups, materials):
    """Declare the mesh groups and materials, then reset any prior state."""
    global GROUPS, MATERIALS, _geometry, _boxes
    GROUPS, MATERIALS = groups, materials
    _geometry = dict((name, {"verts": [], "faces": []}) for name in groups)
    _boxes = []
    check_names_are_distinct()


def set_extent(width, depth):
    """Centre the model on x=width/2, y=depth/2. Pass (0, 0) to keep plan
    coordinates as-is, which is usually what you want for a ship or a
    character where the drawing already has a sensible origin."""
    global _extent
    _extent = (width, depth)


def to_local(x, y):
    """Plan metres -> Blender metres. +Y is north; drawings run y southward."""
    w, d = _extent
    return (x - w / 2.0, d / 2.0 - y)


def check_names_are_distinct():
    """Nothing exported may share a name with anything else exported.

    Unreal imports meshes and materials into one namespace. If a material and
    a mesh are both called Roof, the second one in is silently renamed Roof1 --
    and collision, which is matched by name, then attaches to nothing. An
    intangible mesh looks exactly like a bug in the movement code, which is an
    expensive way to find a naming collision.
    """
    clash = sorted(set(GROUPS) & set(MATERIALS))
    if clash:
        raise SystemExit(
            "mesh groups and materials share a name, which the engine resolves "
            "by renaming one on import: %s" % ", ".join(clash))


# --------------------------------------------------------------------------
# Solids
# --------------------------------------------------------------------------

_FACES = ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
          (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))


def box(group, x0, x1, y0, y1, z0, z1, label=""):
    """An axis-aligned solid in plan coordinates. The label is what the
    checks print, so name it after the thing on the drawing."""
    _boxes.append((group, label, (x0, x1, y0, y1, z0, z1)))
    g = _geometry[group]
    base = len(g["verts"])
    bx0, by1 = to_local(x0, y0)
    bx1, by0 = to_local(x1, y1)
    g["verts"].extend([
        (bx0, by0, z0), (bx1, by0, z0), (bx1, by1, z0), (bx0, by1, z0),
        (bx0, by0, z1), (bx1, by0, z1), (bx1, by1, z1), (bx0, by1, z1),
    ])
    for f in _FACES:
        g["faces"].append(tuple(base + i for i in f))


def wall_ns(group, x, y0, y1, z0, z1, t, label=""):
    """Wall running north-south, dimensioned to its centreline at x."""
    box(group, x - t / 2.0, x + t / 2.0, y0, y1, z0, z1, label)


def wall_ew(group, y, x0, x1, z0, z1, t, label=""):
    """Wall running east-west, dimensioned to its centreline at y."""
    box(group, x0, x1, y - t / 2.0, y + t / 2.0, z0, z1, label)


def cylinder(group, cx, cy, radius, z0, z1, segments=48):
    g = _geometry[group]
    base = len(g["verts"])
    bcx, bcy = to_local(cx, cy)
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


def stair(group, x_start, direction, run, width_y, rise_total, steps,
          knit, sink, axis="x"):
    """A flight whose last tread is the floor it lands on.

    Drawing a final tread level with the destination floor puts two coplanar
    faces in the same plane, which is the z-fighting in the next section. So
    steps-1 treads are drawn and the landing slab is the last one; the final
    riser is the step up onto it. Each tread reaches `knit` into the next.
    Returns the plan coordinate where the stairwell opening must end.
    """
    drawn = steps - 1
    going = run / float(steps)
    for i in range(drawn):
        a = x_start + direction * i * going
        b = x_start + direction * ((i + 1) * going + knit)
        lo, hi = min(a, b), max(a, b)
        if axis == "x":
            box(group, lo, hi, width_y[0], width_y[1], sink,
                (i + 1) * (rise_total / float(steps)), "tread %d" % (i + 1))
        else:
            box(group, width_y[0], width_y[1], lo, hi, sink,
                (i + 1) * (rise_total / float(steps)), "tread %d" % (i + 1))
    return x_start + direction * drawn * going


# --------------------------------------------------------------------------
# Check 1 -- does the model agree with what the source claims?
# --------------------------------------------------------------------------

def check_schedule(measured, published, tolerance=0.51):
    """Compare computed quantities against the numbers the source prints.

    Most design documents carry a schedule, a parts table or a callout with
    real numbers in it. Asserting against them before building turns a
    mis-transcribed coordinate into a failed run instead of something noticed
    in a screenshot days later. Counts (terminals, alcoves, engines) work the
    same way and catch a duplicated or dropped loop iteration.
    """
    print("")
    print("  %-24s %10s %10s" % ("schedule", "built", "source"))
    bad = []
    for name, value in measured.items():
        want = published[name]
        ok = abs(value - want) < tolerance
        print("  %-24s %10.1f %10.1f  %s"
              % (name, value, want, "ok" if ok else "MISMATCH"))
        if not ok:
            bad.append(name)
    if bad:
        raise SystemExit("model disagrees with the source: " + ", ".join(bad))


# --------------------------------------------------------------------------
# Check 2 -- coincident faces (z-fighting)
# --------------------------------------------------------------------------

def _enclosed(ax, plane, rect, exclude):
    """Is this patch sealed inside some third solid? Then it cannot be seen,
    so it cannot flicker, and counting it would drown the real faults."""
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


def report_coincident_faces(limit=14):
    """Find same-facing coplanar faces that two different materials share.

    Two faces pointing the same way at the same depth have nothing to break
    the tie, so the renderer picks per pixel per frame and the surface
    crawls. The fix is never to move a face by hand: it is to make solids
    overlap rather than meet, so one face ends up buried inside the other
    solid. Same-material pairs are reported separately because both
    candidates shade identically -- the depth test ties without flickering.

    Returns the count that can actually be seen, which is what to drive to
    zero.
    """
    found, buried, same_material = [], [0], [0]
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
                    area, rect = 1.0, [None, None, None]
                    for other in range(3):
                        if other == ax:
                            continue
                        lo = max(bi[other * 2], bj[other * 2])
                        hi = min(bi[other * 2 + 1], bj[other * 2 + 1])
                        if hi - lo <= 1e-4:
                            area = 0.0
                            break
                        rect[other] = (lo, hi)
                        area *= hi - lo
                    if area <= 0.0:
                        continue
                    if _enclosed(ax, plane, rect, (i, j)):
                        buried[0] += 1
                        continue
                    if GROUPS[gi][1] == GROUPS[gj][1]:
                        same_material[0] += 1
                        continue
                    found.append((area, AXIS_NAME[ax], side, plane,
                                  gi, li, gj, lj))
    found.sort(reverse=True, key=lambda f: f[0])
    print("")
    print("  coincident, sealed inside a third solid: %d (cannot be seen)"
          % buried[0])
    print("  coincident, same material both sides:    %d (ties, no flicker)"
          % same_material[0])
    if not found:
        print("  coincident across two materials:         none")
        return 0
    print("  coincident across two materials:         %d, worst first"
          % len(found))
    for area, ax, side, plane, gi, li, gj, lj in found[:limit]:
        print("    %8.2f m2  %s=%-8.4g %s  %s vs %s"
              % (area, ax, plane, side, li or gi, lj or gj))
    if len(found) > limit:
        print("    ... and %d more" % (len(found) - limit))
    return len(found)


# --------------------------------------------------------------------------
# Check 3 -- does a character fit?
# --------------------------------------------------------------------------

def report_tight_gaps(floor_z, level, pawn_diameter, pawn_height,
                      min_walkway, obstacle_materials, limit=12):
    """Find places two solids face each other too closely to walk between.

    Plans are dimensioned to centrelines, which is not the space anybody
    walks in: two walls a metre apart on the drawing leave half a metre of
    air once each has taken its thickness. Nothing about the drawing looks
    wrong when a corridor is impassable, so this has to be measured off the
    built solids.

    Pass only the materials that obstruct walking. Floors, slabs, roofs and
    stair treads are surfaces a character stands on or under, and counting a
    tread as an obstacle reports a staircase as two dozen impassable slots.

    Returns the number narrower than the character, which must be zero.
    """
    lo, hi = floor_z + 0.05, floor_z + pawn_height
    live = [(g, l, b) for g, l, b in _boxes
            if GROUPS[g][1] in obstacle_materials
            and b[4] < hi - 1e-6 and b[5] > lo + 1e-6]

    gaps = []
    for i in range(len(live)):
        gi, li, bi = live[i]
        for j in range(i + 1, len(live)):
            gj, lj, bj = live[j]
            for ax, other in ((0, 1), (1, 0)):
                run = min(bi[other * 2 + 1], bj[other * 2 + 1]) - \
                    max(bi[other * 2], bj[other * 2])
                if run < 0.5:
                    continue          # not facing each other across a gap
                if bi[ax * 2 + 1] <= bj[ax * 2]:
                    gap = bj[ax * 2] - bi[ax * 2 + 1]
                elif bj[ax * 2 + 1] <= bi[ax * 2]:
                    gap = bi[ax * 2] - bj[ax * 2 + 1]
                else:
                    continue          # they overlap; no gap to measure
                if 0.01 < gap < min_walkway - 1e-6:
                    gaps.append((gap, run, AXIS_NAME[ax], li or gi, lj or gj))

    gaps.sort(key=lambda g: g[0])
    print("")
    print("  %s: gaps under %.2f m between facing obstacles"
          % (level, min_walkway))
    if not gaps:
        print("    none")
        return 0
    blocked = sum(1 for g in gaps if g[0] < pawn_diameter)
    for gap, run, ax, a, b in gaps[:limit]:
        print("    %5.2f m clear over %5.1f m of %s  %-11s %s vs %s"
              % (gap, run, ax,
                 "IMPASSABLE" if gap < pawn_diameter else "tight", a, b))
    if len(gaps) > limit:
        print("    ... and %d more" % (len(gaps) - limit))
    print("    %d narrower than the %.2f m character" % (blocked, pawn_diameter))
    return blocked


# --------------------------------------------------------------------------
# Check 4 -- what actually got built
# --------------------------------------------------------------------------

def report_bounds(objects):
    lo, hi, faces = [1e9] * 3, [-1e9] * 3, 0
    for obj in objects.values():
        faces += len(obj.data.polygons)
        for v in obj.data.vertices:
            for i in range(3):
                lo[i] = min(lo[i], v.co[i])
                hi[i] = max(hi[i], v.co[i])
    print("")
    for i, ax in enumerate(AXIS_NAME):
        print("  bounds %s %8.2f .. %8.2f  (%.2f m)"
              % (ax.upper(), lo[i], hi[i], hi[i] - lo[i]))
    print("  objects %d, faces %d" % (len(objects), faces))
    return lo, hi


# --------------------------------------------------------------------------
# Collision
# --------------------------------------------------------------------------

def collision_hulls(ramp_groups=None):
    """One convex hull per solid, read from the same list the geometry came
    from so the two cannot drift.

    Every axis-aligned box is already a convex hull, so this is the model
    rather than an approximation of it -- no decomposition, nothing to tune.
    Reads _boxes rather than appending to it, because the coincidence check
    measures that list and collision would report itself as coincident with
    the geometry that generated it.

    ramp_groups maps a group name to a run direction (+1/-1) for stairs, so a
    flight gets one sloped hull a character can walk up instead of two dozen
    box hulls to catch on.
    """
    ramp_groups = ramp_groups or {}
    hulls, ramps = [], {}
    for group, _label, b in _boxes:
        if group in ramp_groups:
            x0, x1, y0, y1, z0, z1 = b
            cur = ramps.get(group)
            if cur is None:
                ramps[group] = [x0, x1, y0, y1, z0, z1]
            else:
                cur[0], cur[1] = min(cur[0], x0), max(cur[1], x1)
                cur[2], cur[3] = min(cur[2], y0), max(cur[3], y1)
                cur[4], cur[5] = min(cur[4], z0), max(cur[5], z1)
            continue
        hulls.append((group, _box_verts(*b)))

    for group, (x0, x1, y0, y1, z0, z1) in ramps.items():
        low, high = (z0, z1) if ramp_groups[group] > 0 else (z1, z0)
        hulls.append((group, _ramp_verts(x0, x1, y0, y1, z0, low, high)))
    return hulls


def _box_verts(x0, x1, y0, y1, z0, z1):
    bx0, by1 = to_local(x0, y0)
    bx1, by0 = to_local(x1, y1)
    return [(bx0, by0, z0), (bx1, by0, z0), (bx1, by1, z0), (bx0, by1, z0),
            (bx0, by0, z1), (bx1, by0, z1), (bx1, by1, z1), (bx0, by1, z1)]


def _ramp_verts(x0, x1, y0, y1, z0, z_west, z_east):
    bx0, by1 = to_local(x0, y0)
    bx1, by0 = to_local(x1, y1)
    return [(bx0, by0, z0), (bx1, by0, z0), (bx1, by1, z0), (bx0, by1, z0),
            (bx0, by0, z_west), (bx1, by0, z_east),
            (bx1, by1, z_east), (bx0, by1, z_west)]


def build_collision(root, subs, ramp_groups=None):
    """Attach one UCX_<mesh>_NN object per hull, in a collection nothing renders.

    Unreal reads objects named UCX_<mesh>_NN out of the FBX as simple
    collision. The suffix matters: the importer matches the mesh by the name
    between the prefix and it. A walking character sweeps against simple
    collision only, so a mesh without it is not roughly solid, it is
    intangible -- and walking through a wall looks exactly like a bug in the
    movement code.
    """
    coll = bpy.data.collections.new("Collision")
    root.children.link(coll)
    subs["Collision"] = coll
    counts, made = {}, []
    for group, verts in collision_hulls(ramp_groups):
        counts[group] = counts.get(group, 0) + 1
        name = "UCX_%s_%02d" % (group, counts[group])
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(verts, [], list(_FACES))
        mesh.validate()
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        coll.objects.link(obj)
        made.append(obj)
    print("  collision: %d hulls over %d meshes" % (len(made), len(counts)))
    return made


# --------------------------------------------------------------------------
# Scene
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
        mat.diffuse_color = rgba            # Workbench reads this one
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = rgba
            if "Roughness" in bsdf.inputs:
                bsdf.inputs["Roughness"].default_value = 0.85
        made[name] = mat
    return made


def build(root_name):
    """One mesh object per group, each in its declared collection."""
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"

    mats = make_materials()
    root = bpy.data.collections.new(root_name)
    scene.collection.children.link(root)

    subs, objects = {}, {}
    for group, (coll_name, mat_name) in GROUPS.items():
        if coll_name not in subs:
            coll = bpy.data.collections.new(coll_name)
            root.children.link(coll)
            subs[coll_name] = coll
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
# Renders
# --------------------------------------------------------------------------

def point_at(obj, target):
    d = Vector(target) - obj.location
    obj.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


def setup_workbench():
    """Flat solid shading. Right for plans and cutaways, wrong for interiors:
    the studio light is view-space, so a ceiling seen from below faces away
    from it and renders black."""
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    s = scene.display.shading
    s.light, s.color_type = "STUDIO", "MATERIAL"
    s.show_cavity, s.cavity_type = True, "BOTH"
    s.show_shadows = s.show_object_outline = True
    scene.display.render_aa = "16"


def setup_eevee(exposure=-0.35):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    try:
        scene.view_settings.view_transform = "AgX"   # rolls off, does not clip
    except TypeError:
        scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = exposure
    for attr in ("use_raytracing", "use_shadows"):
        if hasattr(scene.eevee, attr):
            setattr(scene.eevee, attr, True)


def make_world(colour=(0.48, 0.51, 0.54, 1.0), strength=0.55):
    """Broad ambient. EEVEE gives no bounce here, so an interior lit only from
    ceiling panels goes black on every vertical surface -- the world is doing
    the work the bounce would."""
    world = bpy.data.worlds.new("GB_World")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg is not None:
        bg.inputs["Color"].default_value = colour
        bg.inputs["Strength"].default_value = strength
    bpy.context.scene.world = world


def make_lights(panels):
    """panels: (name, plan_x, plan_y, z, size_x, size_y, watts).

    Keep the watts modest and let the ambient carry the image. Every panel is
    hidden from camera because an area light in frame blows the shot out.
    """
    coll = bpy.data.collections.new("Lights")
    bpy.context.scene.collection.children.link(coll)
    for name, px, py, z, sx, sy, watts in panels:
        data = bpy.data.lights.new(name, type="AREA")
        data.shape, data.size, data.size_y = "RECTANGLE", sx, sy
        data.energy = watts
        obj = bpy.data.objects.new(name, data)
        bx, by = to_local(px, py)
        obj.location = (bx, by, z)
        obj.rotation_euler = (math.pi, 0.0, 0.0)
        obj.visible_camera = False
        coll.objects.link(obj)
    return coll


def render_to(path, width, height):
    scene = bpy.context.scene
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.render.resolution_percentage = 100
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("  wrote %s" % path)


# --------------------------------------------------------------------------
# Export
# --------------------------------------------------------------------------

def export_fbx(path):
    """FBX_SCALE_UNITS is what makes a metre in Blender a metre in Unreal."""
    try:
        bpy.ops.preferences.addon_enable(module="io_scene_fbx")
    except Exception:
        pass
    try:
        bpy.ops.export_scene.fbx(
            filepath=path, use_selection=False, apply_unit_scale=True,
            apply_scale_options="FBX_SCALE_UNITS", object_types={"MESH"},
            mesh_smooth_type="FACE", use_mesh_modifiers=True,
            bake_space_transform=False, axis_forward="-Z", axis_up="Y")
        print("  wrote %s" % path)
        return True
    except Exception as exc:                       # noqa: BLE001
        print("  FBX export failed: %s" % exc)
        return False


def parse_args():
    """--out for the .blend and renders, --fbx for the engine file,
    --check-only to run the measurements and stop.

    Two destinations rather than one because working files usually do not
    belong in the game repository, and a single --out means somebody has to
    remember that every time.
    """
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    def flag(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default

    out = os.path.abspath(flag("--out", os.getcwd()))
    if not os.path.isdir(out):
        os.makedirs(out)
    fbx = flag("--fbx")
    if fbx:
        fbx = os.path.abspath(fbx)
        parent = os.path.dirname(fbx)
        if parent and not os.path.isdir(parent):
            os.makedirs(parent)
    return out, fbx, "--check-only" in argv
