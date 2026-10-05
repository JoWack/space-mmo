#!/usr/bin/env python3
"""
Turn a Tripo export into a character's source .blend: steps 1 to 4 of task 175.

    blender --factory-startup --background --python-exit-code 1 \
        --python tools/characters/tripo_to_blend.py -- \
        --model PATH --name NAME --height M [--yaw DEG] [--sheets PATTERN] [--out DIR] [--force]

  * Imports by what the file is, not its extension: Tripo's quad output is FBX served as .glb (180).
  * Keeps the import as it came, in a collection "Source" excluded from the view layer, so the textured
    high-poly survives whatever the game mesh later becomes.
  * The working copy, named NAME, is turned by --yaw to face -Y (Tripo exports face +X, so -90), scaled
    to --height with its soles at z=0, and centred the way the sheets are registered, which puts the
    origin under the pelvis (compare_to_sheets.place). That is how 180's mesh stood, so the pawn's
    CharacterMeshRotation of yaw -90 serves it.
  * Deletes loose pieces under 1% of the vertices, reporting each: Tripo leaves specks. Then closes the
    small holes a speck leaves where it broke away (up to 16 edges each), and reports any wider opening
    without touching it.
  * With --sheets, scores the result against the sheets (character_build.silhouette_report) and refuses
    to save if the mean IoU is under 0.8, which is what a wrong --yaw or a scrambled multiview run scores.
  * Names the material and textures after NAME, packs the textures, and saves OUT/NAME.blend. It will not
    replace an existing file without --force.
"""

import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(globals().get("__file__", "tools/characters/tripo_to_blend.py")))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import bmesh  # noqa: E402
import bpy  # noqa: E402

import character_build as cb  # noqa: E402
import compare_to_sheets as cts  # noqa: E402

DEFAULT_OUT = "D:/Documents/SpaceMMOAssets/Blender/Characters"
SPECK_FRACTION = 0.01
MAX_HOLE_EDGES = 16      # a speck's scar; anything wider is left open and reported
MIN_MEAN_IOU = 0.8
# Principled BSDF input -> texture suffix
TEXTURE_ROLES = {"Base Color": "BaseColor", "Metallic": "Metallic", "Roughness": "Roughness", "Normal": "Normal"}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    def flag(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default

    args = {"model": flag("--model"), "name": flag("--name"), "height": flag("--height"),
            "yaw": float(flag("--yaw", "-90")), "sheets": flag("--sheets"),
            "out": os.path.abspath(flag("--out", DEFAULT_OUT)), "force": "--force" in argv}
    if not (args["model"] and args["name"] and args["height"]):
        sys.exit("tripo_to_blend: --model, --name and --height are required")
    args["model"] = os.path.abspath(args["model"])
    args["height"] = float(args["height"])
    if args["sheets"]:
        args["sheets"] = os.path.abspath(args["sheets"])
    return args


def keep_source(obj, name):
    """An untouched copy of the import, in a collection the view layer excludes."""
    coll = bpy.data.collections.new("Source")
    bpy.context.scene.collection.children.link(coll)
    src = obj.copy()
    src.data = obj.data.copy()
    src.name = src.data.name = name + "_TripoSource"
    coll.objects.link(src)
    bpy.context.view_layer.layer_collection.children["Source"].exclude = True
    return src


def remove_specks(obj):
    """Delete connected pieces smaller than SPECK_FRACTION of the vertices. Returns their sizes."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    seen, pieces = set(), []
    for v in bm.verts:
        if v.index in seen:
            continue
        piece, stack = [], [v]
        while stack:
            x = stack.pop()
            if x.index in seen:
                continue
            seen.add(x.index)
            piece.append(x)
            stack.extend(e.other_vert(x) for e in x.link_edges)
        pieces.append(piece)
    limit = SPECK_FRACTION * len(bm.verts)
    specks = [p for p in pieces if len(p) < limit]
    removed = []
    for p in specks:
        zs = [x.co.z for x in p]
        removed.append({"verts": len(p), "z": [round(min(zs), 3), round(max(zs), 3)]})
    bmesh.ops.delete(bm, geom=[x for p in specks for x in p], context="VERTS")
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    return removed


def fill_small_holes(obj, max_edges=MAX_HOLE_EDGES):
    """Close boundary loops of up to max_edges edges, triangulated. Returns each filled loop's size,
    and the sizes of any larger openings, which are left alone for a person to look at."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    boundary = [e for e in bm.edges if e.is_boundary]
    loops, seen = [], set()
    for e in boundary:
        if e.index in seen:
            continue
        loop, stack = [], [e]
        while stack:
            x = stack.pop()
            if x.index in seen:
                continue
            seen.add(x.index)
            loop.append(x)
            stack.extend(n for v in x.verts for n in v.link_edges if n.is_boundary and n.index not in seen)
        loops.append(loop)
    filled, left = [], []
    for loop in loops:
        if len(loop) > max_edges:
            left.append(len(loop))
            continue
        faces = bmesh.ops.holes_fill(bm, edges=loop, sides=0)["faces"]
        bmesh.ops.triangulate(bm, faces=faces)
        filled.append(len(loop))
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    return filled, left


def name_material(obj, name):
    """MAT_<name>, and T_<name>_<role> for each texture by the BSDF input it drives. Packs them."""
    named = {}
    for slot in obj.material_slots:
        mat = slot.material
        if not mat or not mat.use_nodes:
            continue
        mat.name = "MAT_" + name
        bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf is None:
            continue
        for socket, role in TEXTURE_ROLES.items():
            inp = bsdf.inputs.get(socket)
            if not inp or not inp.is_linked:
                continue
            node = inp.links[0].from_node
            while node.type != "TEX_IMAGE" and node.inputs and any(i.is_linked for i in node.inputs):
                node = next(i for i in node.inputs if i.is_linked).links[0].from_node   # through Normal Map
            if node.type == "TEX_IMAGE" and node.image:
                node.image.name = "T_%s_%s" % (name, role)
                named[role] = {"size": list(node.image.size), "colorspace": node.image.colorspace_settings.name}
    for img in bpy.data.images:
        if img.size[0] > 0 and not img.packed_file:
            img.pack()
    return named


def main():
    args = parse_args()
    name, H = args["name"], args["height"]
    path = os.path.join(args["out"], name + ".blend")
    if os.path.exists(path) and not args["force"]:
        sys.exit("tripo_to_blend: %s exists; pass --force to replace it" % path)
    cts.clear()
    obj = cts.import_model(args["model"])
    keep_source(obj, name)
    obj.name = obj.data.name = name
    cts.place(obj, args["yaw"], H)
    removed = remove_specks(obj)
    filled, left_open = fill_small_holes(obj)
    textures = name_material(obj, name)
    bpy.context.view_layer.update()
    report = {"model": args["model"], "yaw": args["yaw"], "height": H, "specks_removed": removed,
              "holes_filled": filled, "openings_left": left_open, "textures": textures, **cb.mesh_stats(obj)}
    renders = os.path.join(args["out"], name + "_renders")
    if args["sheets"]:
        views, _lm = cb.measure(args["sheets"], H)
        iou = cb.silhouette_report(obj, views, H, renders, "import")
        report["iou"] = iou
        mean = sum(iou.values()) / len(iou)
        if mean < MIN_MEAN_IOU:
            sys.exit("tripo_to_blend: mean IoU %.3f is under %.1f: wrong --yaw, or views in the wrong slots. "
                     "Nothing saved." % (mean, MIN_MEAN_IOU))
    for o in list(bpy.context.scene.objects):
        if o.type == "CAMERA":
            bpy.data.objects.remove(o, do_unlink=True)
    os.makedirs(args["out"], exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False)
    os.makedirs(renders, exist_ok=True)
    with open(os.path.join(renders, name + "_import.json"), "w") as f:
        json.dump(report, f, indent=1)
    print("TRIPO saved %s" % path)
    print("TRIPO " + json.dumps({k: report[k] for k in ("specks_removed", "holes_filled", "openings_left", "verts", "faces",
                                                        "triangles_when_triangulated", "components",
                                                        "nonmanifold_edges", "boundary_edges", "bounds")}))
    if "iou" in report:
        print("TRIPO iou " + json.dumps(report["iou"]))
    print("TRIPO textures " + json.dumps(textures))


if __name__ == "__main__":
    main()
