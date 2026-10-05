#!/usr/bin/env python3
"""
Score character meshes against their four sheets, on the terms 180's own build is scored on. Task 180.

    blender --factory-startup --background --python-exit-code 1 \
        --python tools/characters/compare_to_sheets.py -- \
        --model PATH[:YAW] [--model PATH[:YAW] ...] [--sheets PATTERN] [--height M] [--out DIR]

For each model: the silhouette IoU against every sheet (character_build.silhouette_report, the same
function behind the build's limits, so 180's FBX scores exactly what its record says), the mesh counts
(character_build.mesh_stats), a render from each sheet's own camera, and a three-quarter and a side
view of the head, textured and in clay. Then two grids: every view beside its sheet, and the heads.

YAW turns a model to face -Y, which is how 180's mesh stands and where the sheet cameras look from.
  * `keep`, or no YAW: used exactly where it stands. 180's FBX is already registered to the sheets.
  * a number of degrees: turned by that much, scaled to --height with the soles at z=0, and centred
    the way sheet_measure registers the sheets -- x on the torso's middle over 0.56-0.66 H, y on the
    hips' middle over 0.47-0.50 H. Tripo H3.1 exports face +X, so they take -90. Check the front
    render shows a face before believing any number.

A file that starts "Kaydara FBX Binary" is imported as FBX whatever its extension: Tripo returns FBX
when asked for quads, and Higgsfield still names the download .glb. Blender's glTF importer then
fails with "Bad glTF: json error: utf-8", which reads like a corrupt download and is not one.
"""

import json
import math
import os
import sys

_HERE = os.path.dirname(os.path.abspath(globals().get("__file__", "tools/characters/compare_to_sheets.py")))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import character_build as cb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(_HERE))
DEFAULT_SHEETS = "D:/Documents/SpaceMMOAssets/CharacterImages/Humanoid-Male/humanoid-male-%s.png"
DEFAULT_OUT = os.path.join(ROOT, "docs", "wip", "compare")
VIEWS = ("front", "left", "back", "right")


def _model_spec(spec):
    """PATH, PATH:keep or PATH:YAW. A drive letter's colon is followed by a slash; a yaw's is not."""
    head, sep, tail = spec.rpartition(":")
    if sep and tail == "keep":
        return head, None
    if sep and head and not tail.startswith(("/", "\\")):
        try:
            return head, float(tail)
        except ValueError:
            pass
    return spec, None


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    models, args = [], {"sheets": DEFAULT_SHEETS, "height": 1.80, "out": DEFAULT_OUT}
    i = 0
    while i < len(argv):
        if argv[i] == "--model":
            models.append(_model_spec(argv[i + 1]))
        elif argv[i] == "--sheets":
            args["sheets"] = argv[i + 1]
        elif argv[i] == "--height":
            args["height"] = float(argv[i + 1])
        elif argv[i] == "--out":
            args["out"] = argv[i + 1]
        i += 2
    if not models:
        sys.exit("compare_to_sheets: give at least one --model PATH[:YAW]")
    # Blender writes a relative image path under the root of C:, not the working directory, and
    # reads it back from there, so every number comes out right while the renders land elsewhere.
    args["models"] = [(os.path.abspath(p), yaw) for p, yaw in models]
    args["out"] = os.path.abspath(args["out"])
    args["sheets"] = os.path.abspath(args["sheets"])
    return args


def clear():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.cameras):
        for d in list(coll):
            if d.users == 0:
                coll.remove(d)


def import_model(path):
    """Import by what the file is, not what it is called. Returns one mesh object."""
    with open(path, "rb") as f:
        head = f.read(18)
    if head.startswith(b"Kaydara FBX Binary") or path.lower().endswith(".fbx"):
        bpy.ops.import_scene.fbx(filepath=path)
    elif path.lower().endswith((".glb", ".gltf")):
        bpy.ops.import_scene.gltf(filepath=path)
    elif path.lower().endswith(".obj"):
        bpy.ops.wm.obj_import(filepath=path)
    else:
        raise RuntimeError("compare_to_sheets: cannot tell what %s is" % path)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    for o in meshes:
        o.data.transform(o.matrix_world)
        o.matrix_world = Matrix.Identity(4)
        o.parent = None
    if len(meshes) > 1:
        bpy.ops.object.select_all(action="DESELECT")
        for o in meshes:
            o.select_set(True)
        bpy.context.view_layer.objects.active = meshes[0]
        bpy.ops.object.join()
    return meshes[0]


def place(obj, yaw, H):
    """Turn to face -Y, scale to H, soles at 0, and centre on the sheets' registration axes."""
    me = obj.data
    me.transform(Matrix.Rotation(math.radians(yaw), 4, "Z"))
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    co *= H / (co[:, 2].max() - co[:, 2].min())
    co[:, 2] -= co[:, 2].min()
    band = co[(co[:, 2] > 0.56 * H) & (co[:, 2] < 0.66 * H)]
    mid = 0.5 * (co[:, 0].min() + co[:, 0].max())
    torso = band[np.abs(band[:, 0] - mid) < 0.20]          # arms hang clear of the torso here
    cx = 0.5 * (torso[:, 0].min() + torso[:, 0].max())
    hips = co[(co[:, 2] > 0.47 * H) & (co[:, 2] < 0.50 * H) & (np.abs(co[:, 0] - cx) < 0.17)]
    cy = 0.5 * (hips[:, 1].min() + hips[:, 1].max())        # the hands are excluded by the 0.17
    co[:, 0] -= cx
    co[:, 1] -= cy
    me.vertices.foreach_set("co", co.ravel())
    me.update()


def _workbench(textured):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.view_settings.view_transform = "Standard"
    sc.render.film_transparent = False
    sc.display.render_aa = "8"
    if sc.world is None:
        sc.world = bpy.data.worlds.new("CompareWorld")
    sc.world.color = (0.82, 0.80, 0.82)
    sh = sc.display.shading
    sh.show_cavity, sh.show_object_outline = False, False
    if textured:
        sh.light, sh.color_type = "FLAT", "TEXTURE"
    else:
        sh.light, sh.color_type, sh.single_color = "STUDIO", "SINGLE", (0.78, 0.78, 0.78)
    return sc


def render_sheet_views(views, H, out, tag):
    sc = _workbench(True)
    cd = bpy.data.cameras.get("SheetCam") or bpy.data.cameras.new("SheetCam")
    cam = bpy.data.objects.get("SheetCam") or bpy.data.objects.new("SheetCam", cd)
    if cam.name not in sc.collection.objects:
        sc.collection.objects.link(cam)
    cd.type, cd.sensor_fit, cd.clip_start, cd.clip_end = "ORTHO", "AUTO", 0.1, 100.0
    sc.camera = cam
    for name, view in views.items():
        sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = view.w, view.h, 100
        cd.ortho_scale = max(view.w, view.h) * view.scale
        cam.location, cam.rotation_euler = cb._sheet_camera(view, H)
        sc.render.filepath = os.path.join(out, "%s_sheetcam_%s.png" % (tag, name))
        bpy.ops.render.render(write_still=True)


def render_head(obj, out, tag):
    sc = bpy.context.scene
    cd = bpy.data.cameras.new("HeadCam")
    cd.lens = 85
    cam = bpy.data.objects.new("HeadCam", cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    top = max(v.co.z for v in obj.data.vertices)
    target = Vector((0.0, -0.02, top - 0.13))
    for vn, d in (("34", Vector((0.55, -1.0, 0.08))), ("side", Vector((1.0, 0.0, 0.05)))):
        d.normalize()
        cam.location = target + d * 1.15
        cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
        for textured, mode in ((True, "tex"), (False, "clay")):
            sc = _workbench(textured)
            sc.render.resolution_x = sc.render.resolution_y = 700
            sc.render.resolution_percentage = 100
            sc.render.filepath = os.path.join(out, "%s_head_%s_%s.png" % (tag, vn, mode))
            bpy.ops.render.render(write_still=True)


def _load(path):
    im = bpy.data.images.load(path, check_existing=False)
    w, h = im.size
    px = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(px)
    bpy.data.images.remove(im)
    px = px.reshape(h, w, 4)
    px[..., 3] = 1.0
    return px


def _shrink(a, f):
    h, w = a.shape[:2]
    ys = (np.arange(int(h * f)) / f).astype(int)
    xs = (np.arange(int(w * f)) / f).astype(int)
    return a[ys][:, xs]


def _save(px, path):
    h, w = px.shape[:2]
    img = bpy.data.images.new("_compare_grid", w, h, alpha=True)
    img.pixels.foreach_set(px.ravel())
    img.filepath_raw, img.file_format = path, "PNG"
    img.save()
    bpy.data.images.remove(img)


def grids(sheets, tags, out):
    """views_grid.png: one row per view, the sheet first. heads_grid.png: one row per model."""
    rows = []
    for v in VIEWS:
        cells = [_load(sheets % v)] + [_load(os.path.join(out, "%s_sheetcam_%s.png" % (t, v))) for t in tags]
        cells = [_shrink(c[:, int(c.shape[1] * 0.12):int(c.shape[1] * 0.88)], 0.22) for c in cells]
        for c in cells:
            c[:, :2, :3] = 0.1
        rows.append(np.concatenate(cells, axis=1))
    _save(np.concatenate(rows[::-1], axis=0), os.path.join(out, "views_grid.png"))   # rows run bottom-up
    heads = []
    for t in tags:
        cells = [_shrink(_load(os.path.join(out, "%s_head_%s_%s.png" % (t, vn, m))), 0.6)
                 for vn in ("34", "side") for m in ("tex", "clay")]
        for c in cells:
            c[:, :2, :3] = 0.1
        heads.append(np.concatenate(cells, axis=1))
    _save(np.concatenate(heads[::-1], axis=0), os.path.join(out, "heads_grid.png"))


def main():
    args = parse_args()
    H, out = args["height"], args["out"]
    os.makedirs(out, exist_ok=True)
    views, _lm = cb.measure(args["sheets"], H)
    results, tags = {}, []
    for path, yaw in args["models"]:
        tag = os.path.splitext(os.path.basename(path))[0]
        clear()
        obj = import_model(path)
        if yaw is not None:
            place(obj, yaw, H)
        bpy.context.view_layer.update()
        iou = cb.silhouette_report(obj, views, H, out, tag)
        stats = cb.mesh_stats(obj)
        render_sheet_views(views, H, out, tag)
        render_head(obj, out, tag)
        results[tag] = {"path": path, "yaw": yaw, "iou": iou, **stats}
        tags.append(tag)
        print("COMPARE %s iou %s  triangles %d  components %d  nonmanifold %d" % (
            tag, " ".join("%s=%.3f" % (v, iou[v]) for v in VIEWS),
            stats["triangles_when_triangulated"], stats["components"], stats["nonmanifold_edges"]))
    grids(args["sheets"], tags, out)
    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump(results, f, indent=1)
    print("COMPARE wrote %s" % out)


if __name__ == "__main__":     # Blender runs --python scripts as __main__; importing it runs nothing
    main()
