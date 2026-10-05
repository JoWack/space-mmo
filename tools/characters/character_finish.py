"""
Finishing a character built by character_build: the checks that decide whether it ships, the
renders that show it beside its sheets, the .blend, the engine FBX and the joints manifest.
"""

import json
import math
import os

import bpy
import numpy as np
from mathutils import Vector

from character_build import PARTS, log, mesh_stats, mirror_pairs, _sheet_camera


# ==================================================================================================
# Checks
# ==================================================================================================

def run_checks(obj, body, iou, uv, tex, cfg):
    """Every number that says the mesh is game-ready and matches its sheets, against a limit.
    Returns [(name, value, limit, ok)]. The build refuses to save on any failure."""
    H = body.H
    s = mesh_stats(obj)
    _, worst = mirror_pairs(obj, tol=1e-4)
    lim = cfg.get("checks", {})
    rows = [
        ("one piece", s["components"], "== 1", s["components"] == 1),
        ("closed (boundary edges)", s["boundary_edges"], "== 0", s["boundary_edges"] == 0),
        ("manifold (non-manifold edges)", s["nonmanifold_edges"], "== 0", s["nonmanifold_edges"] == 0),
        ("genus 0 (Euler characteristic)", s["euler"], "== 2", s["euler"] == 2),
        ("normals outward (signed volume m3)", s["signed_volume_m3"], "> 0", s["signed_volume_m3"] > 0),
        ("all quads (tris + ngons)", s["tris"] + s["ngons"], "== 0", s["tris"] + s["ngons"] == 0),
        ("triangles in engine", s["triangles_when_triangulated"], "<= %d" % lim.get("max_tris", 16000),
         s["triangles_when_triangulated"] <= lim.get("max_tris", 16000)),
        ("height m", round(s["bounds"][1][2], 4), "%.3f..%.3f" % (H - 0.012, H + 0.002),
         H - 0.012 <= s["bounds"][1][2] <= H + 0.002),
        ("soles on the ground (min z, mm)", round(1000 * s["bounds"][0][2], 2), "|z| <= 1",
         abs(s["bounds"][0][2]) <= 0.001),
        ("symmetric (worst mirror partner, mm)", round(1000 * worst, 3), "<= 0.1", worst <= 1e-4),
        ("UV corners outside 0..1", uv["corners_outside"], "== 0", uv["corners_outside"] == 0),
        ("UV coverage of the square", uv["coverage"], ">= %.2f" % lim.get("min_uv", 0.5),
         uv["coverage"] >= lim.get("min_uv", 0.5)),
        ("texels no sheet saw well", round(tex["fallback"], 3), "<= %.2f" % lim.get("max_fallback", 0.25),
         tex["fallback"] <= lim.get("max_fallback", 0.25)),
    ]
    for view, floor in lim.get("min_iou", {"front": 0.90, "back": 0.93, "left": 0.94, "right": 0.94}).items():
        rows.append(("silhouette IoU, %s sheet" % view, round(float(iou[view]), 4), ">= %.2f" % floor,
                     float(iou[view]) >= floor))
    width = max(len(r[0]) for r in rows)
    log("checks:")
    for name, value, limit, ok in rows:
        log("  %-*s %12s   %-12s %s" % (width, name, value, limit, "ok" if ok else "FAIL"))
    return rows


# ==================================================================================================
# Renders
# ==================================================================================================

def _look_scene():
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    try:
        scene.view_settings.view_transform = "Standard"
    except TypeError:
        pass
    scene.view_settings.exposure = 0.0
    scene.render.film_transparent = False
    world = bpy.data.worlds.get("_look_world") or bpy.data.worlds.new("_look_world")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (0.42, 0.42, 0.45, 1.0)
    bg.inputs["Strength"].default_value = 1.0
    scene.world = world
    key = bpy.data.objects.get("_key")
    if key is None:
        kd = bpy.data.lights.new("_key", "SUN")
        kd.energy = 2.2
        kd.angle = math.radians(25)
        key = bpy.data.objects.new("_key", kd)
        scene.collection.objects.link(key)
    key.rotation_euler = (math.radians(55), 0, math.radians(-30))
    cam_data = bpy.data.cameras.get("LookCam") or bpy.data.cameras.new("LookCam")
    cam = bpy.data.objects.get("LookCam") or bpy.data.objects.new("LookCam", cam_data)
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    scene.camera = cam
    return scene, cam, key


def _load_png(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return (np.clip(px.reshape(h, w, 4)[::-1, :, :3], 0, 1) * 255).astype(np.uint8)


def _save_png(rgb, path):
    h, w, _ = rgb.shape
    img = bpy.data.images.new("_save", w, h, alpha=False)
    px = np.ones((h, w, 4), dtype=np.float32)
    px[..., :3] = rgb[::-1].astype(np.float32) / 255.0
    img.pixels.foreach_set(px.reshape(-1))
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def renders(obj, body, views, out_dir, name):
    """The textured model from each sheet's own camera, beside that sheet (so any difference is
    the model's), plus two three-quarter views. Returns the paths written."""
    scene, cam, key = _look_scene()
    os.makedirs(out_dir, exist_ok=True)
    paths = []
    tiles = []
    for vn in ("front", "left", "back", "right"):
        v = views[vn]
        scene.render.resolution_x, scene.render.resolution_y = v.w // 2, v.h // 2
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = max(v.w, v.h) * v.scale
        cam.location, cam.rotation_euler = _sheet_camera(v, body.H)
        path = os.path.join(out_dir, "%s_%s.png" % (name, vn))
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        paths.append(path)
        model = _load_png(path)
        sheet = v.rgb[::2, ::2][:model.shape[0], :model.shape[1]]
        tiles.append(np.concatenate([sheet, model], axis=1))
    pair_h = max(t.shape[0] for t in tiles)
    gap = np.full((pair_h, 24, 3), 30, dtype=np.uint8)
    top = np.concatenate([tiles[0], gap, tiles[1]], axis=1)
    bot = np.concatenate([tiles[2], gap, tiles[3]], axis=1)
    sheet_path = os.path.join(out_dir, "%s_vs_sheets.png" % name)
    _save_png(np.concatenate([top, np.full((24, top.shape[1], 3), 30, dtype=np.uint8), bot], axis=0),
              sheet_path)
    paths.append(sheet_path)
    scene.render.resolution_x, scene.render.resolution_y = 1000, 1300
    for tag, loc in (("threequarter_front", (2.6, -3.9, 1.25)), ("threequarter_back", (-2.6, 3.9, 1.25))):
        cam.data.type = "PERSP"
        cam.data.lens = 60
        cam.location = loc
        cam.rotation_euler = (Vector((0, 0, 0.92)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
        path = os.path.join(out_dir, "%s_%s.png" % (name, tag))
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        paths.append(path)
    scene.render.resolution_x, scene.render.resolution_y = 900, 900
    cam.data.lens = 85
    cam.location = (0.25, -0.75, 1.68)
    cam.rotation_euler = (Vector((0, 0, 1.67)) - Vector(cam.location)).to_track_quat("-Z", "Y").to_euler()
    path = os.path.join(out_dir, "%s_head.png" % name)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    paths.append(path)
    for o in (key, cam):
        bpy.data.objects.remove(o, do_unlink=True)
    return paths


# ==================================================================================================
# Outputs
# ==================================================================================================

def joints_manifest(body, path):
    """Where the joints are, for whoever rigs this (task 175): measured off the sheets by the same
    code that built the mesh, so the bones and the edge loops agree. Left side; the right mirrors
    it. Metres, Blender frame (faces -Y, +Z up)."""
    lm, H = body.lm, body.H
    S, W, T = body.S, body.W, body.T
    elbow = S + (W - S) * 0.55
    hip_z = lm["crotch"] + 0.06 * H / 1.8
    hip_x, hip_y, _, _ = body.leg_section(lm["crotch"] - 0.03, 1.0)
    knee_z = 0.50 * H / 1.8
    knee_x, knee_y, _, _ = body.leg_section(knee_z, 1.0)
    ank_x, ank_y, _, _ = body.leg_section(body.ankle_z + 0.02, 1.0)
    # body.ankle_z is where the boot's front leaves the shaft for the foot -- the instep, which is
    # what the foot's rings are placed by. The joint itself is lower: the ankle bone stands about
    # 0.04 of the body's height off the ground, plus the boot's sole, so 0.05 H.
    ankle_joint_z = 0.05 * H
    _, toe_y, _, ry = body.leg_section(0.03, 1.0)
    _, neck_y, _, _ = body.torso_section(body.collar_z)
    _, head_y, _, _ = body.torso_section(lm["neck"] + 0.08)
    data = {
        "_comment": "Joint positions measured off task 174's sheets by tools/characters. Left side "
                    "(+X); mirror x for the right. Metres; the character faces -Y with +Z up and "
                    "stands with its soles at z=0. The mesh has rings either side of each joint.",
        "height": H,
        "pelvis": [0.0, round(hip_y, 4), round(hip_z, 4)],
        "hip_l": [round(hip_x, 4), round(hip_y, 4), round(hip_z, 4)],
        "knee_l": [round(knee_x, 4), round(knee_y, 4), round(knee_z, 4)],
        "ankle_l": [round(ank_x, 4), round(ank_y, 4), round(ankle_joint_z, 4)],
        "toe_l": [round(ank_x, 4), round(toe_y - ry * 0.8, 4), 0.03],
        "shoulder_l": [round(c, 4) for c in S],
        "elbow_l": [round(c, 4) for c in elbow],
        "wrist_l": [round(c, 4) for c in W],
        "middle_fingertip_l": [round(c, 4) for c in T],
        "neck_base": [0.0, round(neck_y, 4), round(body.collar_z, 4)],
        "head": [0.0, round(head_y, 4), round(lm["neck"] + 0.08, 4)],
        "head_top": [0.0, round(head_y, 4), H],
        # "chin" is left out: that heuristic lands at the mouth and drives nothing
        "landmarks": {k: round(v, 4) for k, v in lm.items() if isinstance(v, float) and k != "chin"},
        "instep": round(body.ankle_z, 4),
    }
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2)
    return data


def strip_build_attributes(obj):
    """The part weights steered the fit; nothing downstream needs them."""
    for part in PARTS:
        a = obj.data.attributes.get("w_" + part)
        if a is not None:
            obj.data.attributes.remove(a)


def save_blend(path, image):
    """The working file: the character, its material and its texture packed in, and nothing the
    build used along the way (the sheet and look cameras, the key light, the bake worlds)."""
    for name in ("SheetCam", "CharCam", "LookCam", "_key"):
        o = bpy.data.objects.get(name)
        if o is not None:
            bpy.data.objects.remove(o, do_unlink=True)
    for name in ("SheetCam", "CharCam", "LookCam"):
        c = bpy.data.cameras.get(name)
        if c is not None:
            bpy.data.cameras.remove(c)
    for name in ("_look_world", "_bake_world"):
        w = bpy.data.worlds.get(name)
        if w is not None and bpy.context.scene.world != w:
            bpy.data.worlds.remove(w)
    image.pack()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, copy=True)


def export_fbx(obj, path):
    """FBX for Unreal. FBX_SCALE_UNITS makes a metre in Blender a metre in Unreal (the importer
    needs Convert Scene Unit, see the greybox skill's unreal.md). Blender's default axes are what
    Unreal's importer expects: a mesh facing -Y here faces +Y there, the mannequin's convention, and
    the pawn's CharacterMeshRotation yaw of -90 turns it to face +X."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for o in bpy.context.view_layer.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"MESH"},
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        axis_forward="-Z", axis_up="Y", bake_space_transform=False,
        mesh_smooth_type="FACE", use_mesh_modifiers=True, use_tspace=True,
        add_leaf_bones=False, bake_anim=False, path_mode="RELATIVE", embed_textures=False)
