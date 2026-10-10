"""
Turn Tripo's deposit models into what Unreal imports: task 189.

    blender --background --python tools/deposits/prepare_deposits.py

or run its body through the Blender MCP. Regenerable: everything below comes from the five GLBs in
client/RawContent/Deposits/Tripo/, which are Tripo H3.1 image-to-3D outputs (Higgsfield, 10 October
2026) of the concepts Joe chose, kept beside them as <ore>_concept.png.

For each deposit it writes client/RawContent/Deposits/<Name>/:
  * SM_Deposit_<Name>.fbx -- the mesh, sized to the box the game fits a deposit into and stood on its
    base, plus one UCX_ convex hull for simple collision. Without simple collision nothing can bump into
    a deposit, however solid it looks: that was task 130, and the old two had to be fixed by hand.
  * T_Deposit_<Name>_BaseColor/ORM/Normal.png -- Tripo's own maps, unchanged. ORM is occlusion,
    roughness, metallic in R, G, B; the normal map is OpenGL's convention, which Unreal flips on import.
  * T_Deposit_<Name>_Emissive.png, for the deposits Joe chose to glow (10 October): the texture's own
    colour where it is painted hot, black everywhere else.
and client/RawContent/Deposits/deposits.json, which ue_import_deposits.py reads: each deposit's item key,
names, and the material settings below.
"""

import json
import os

import bmesh
import bpy
import numpy as np

ROOT = "D:/Programming/SpaceMMO/client/RawContent/Deposits"

# The box SpaceMMODepositPlacement.h fits every deposit into, in metres. The game rescales whatever it
# is given, so this only makes the imported asset the size it will be drawn at -- which keeps the
# collision hull and the mesh one scale apart from nothing.
TARGET_WIDTH_M = 2.0
TARGET_HEIGHT_M = 3.0

# Material settings, per deposit. Tripo painted every surface of the ferrite and the scrap about half
# metallic (mean 0.47), stone and paint included, which reads as oil; so metallic is remapped through a
# smoothstep that keeps only the brightest of it -- the ferrite's veins, the scrap's bare steel -- and
# roughness is floored. Measured from the ORM maps on 10 October.
DEPOSITS = [
    {
        "source": "ferrite", "name": "FerriteOre", "item": "ferrite_ore",
        "metal_low": 0.55, "metal_high": 0.80, "rough_min": 0.35,
        "emissive": None, "emissive_strength": 0.0, "hull_top": 1.0,
    },
    {
        "source": "scrap", "name": "ScrapAlloy", "item": "scrap_alloy",
        "metal_low": 0.45, "metal_high": 0.75, "rough_min": 0.30,
        "emissive": None, "emissive_strength": 0.0, "hull_top": 1.0,
    },
    {
        # No metal anywhere in Tripo's map, and none wanted: a salt.
        "source": "oxide", "name": "OxideSalt", "item": "oxide_salt",
        "metal_low": 1.0, "metal_high": 1.01, "rough_min": 0.30,
        "emissive": None, "emissive_strength": 0.0, "hull_top": 1.0,
    },
    {
        # Tripo put stray metal on the sap; there is none in a living rock. The hull stops below the
        # leaning root, or the root's whole sweep would be an invisible wall.
        "source": "rootstone", "name": "Rootstone", "item": "rootstone",
        "metal_low": 1.0, "metal_high": 1.01, "rough_min": 0.45,
        # 6, not the 3 first tried: its sap and root tips are under 1% of the map and at 3 the glow
        # was a few specks in a night render (10 October). A setting on the instance, to tune in play.
        "emissive": "rootstone", "emissive_strength": 6.0, "hull_top": 0.7,
    },
    {
        "source": "blackiron", "name": "BlackironOre", "item": "blackiron_ore",
        "metal_low": 1.0, "metal_high": 1.01, "rough_min": 0.40,
        "emissive": "blackiron", "emissive_strength": 6.0, "hull_top": 1.0,
    },
]

# Few enough points that Unreal's hull is cheap, enough that it follows the rock.
HULL_POINTS = 48


def hsv(px):
    """Hue in degrees, saturation and value, for an (..., 3+) float array in 0..1."""
    r, g, b = px[..., 0], px[..., 1], px[..., 2]
    mx = px[..., :3].max(axis=-1)
    mn = px[..., :3].min(axis=-1)
    d = mx - mn
    h = np.zeros_like(mx)
    nz = d > 1e-6
    rm = nz & (mx == r)
    gm = nz & (mx == g) & ~rm
    bm = nz & ~rm & ~gm
    h[rm] = ((g - b)[rm] / d[rm]) % 6
    h[gm] = ((b - r)[gm] / d[gm]) + 2
    h[bm] = ((r - g)[bm] / d[bm]) + 4
    s = np.where(mx > 1e-6, d / np.maximum(mx, 1e-6), 0)
    return h * 60.0, s, mx


def emissive_mask(kind, px):
    """Where the texture is painted hot.

    Blackiron: its ember seams, saturated orange-red and bright -- 8% of the map, nothing on the stone.
    Rootstone: its amber sap (bright saturated yellow-orange) and its cyan root tips. The wood is the
    same hue family as the sap and must not glow, so the sap's threshold sits above the wood's
    saturation and brightness; checked on the map before it was trusted (10 October).
    """
    h, s, v = hsv(px)
    if kind == "blackiron":
        return ((h < 40) | (h > 345)) & (s > 0.55) & (v > 0.35)
    if kind == "rootstone":
        sap = (h > 28) & (h < 55) & (s > 0.6) & (v > 0.7)
        cyan = (h > 155) & (h < 200) & (s > 0.3) & (v > 0.55)
        return sap | cyan
    raise ValueError(kind)


def save_image(pixels, width, height, path):
    image = bpy.data.images.new(os.path.basename(path), width, height, alpha=True)
    image.pixels[:] = pixels.ravel()
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)


def farthest_points(points, count):
    """A spread-out subset, so a hull of few points still follows the outline."""
    if len(points) <= count:
        return points
    chosen = [int(np.argmax(points[:, 2]))]
    dist = np.linalg.norm(points - points[chosen[0]], axis=1)
    for _ in range(count - 1):
        nxt = int(np.argmax(dist))
        chosen.append(nxt)
        dist = np.minimum(dist, np.linalg.norm(points - points[nxt], axis=1))
    return points[chosen]


def prepare(spec, scene):
    name = spec["name"]
    out = os.path.join(ROOT, name)
    os.makedirs(out, exist_ok=True)

    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, "Tripo", spec["source"] + ".glb"))
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH"]
    assert len(meshes) == 1, "%s: expected one mesh, got %d" % (name, len(meshes))
    obj = meshes[0]

    # Bake the import's transform into the vertices, then fit and seat the mesh.
    mesh = obj.data
    mesh.transform(obj.matrix_world)
    obj.matrix_world.identity()
    co = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
    mesh.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    mn, mx = co.min(axis=0), co.max(axis=0)
    size = mx - mn
    scale = min(TARGET_WIDTH_M / max(size[0], size[1]), TARGET_HEIGHT_M / size[2])
    co = (co - np.array([(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, mn[2]])) * scale
    mesh.vertices.foreach_set("co", co.ravel())
    mesh.update()
    for poly in mesh.polygons:
        poly.use_smooth = True

    for o in new:
        if o is not obj:
            bpy.data.objects.remove(o)

    # Names are the contract with Unreal, which pairs UCX_<mesh>_NN with <mesh>. Blender gives a taken
    # name a ".001" instead, silently, and the hull then belongs to nothing -- which is what happened the
    # first time this ran beside a scene still holding the last run's objects. So clear the names first,
    # and refuse to carry on if one still did not take.
    wanted = "SM_Deposit_" + name
    for stale in (wanted, "UCX_%s_00" % wanted):
        if stale in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[stale])
        if stale in bpy.data.meshes:
            bpy.data.meshes.remove(bpy.data.meshes[stale])
    obj.name = wanted
    mesh.name = wanted
    assert obj.name == wanted, "%s: Blender named it %s" % (name, obj.name)
    obj.data.materials[0].name = "M_Deposit_" + name

    # Simple collision: one convex hull, under hull_top of the height.
    height = co[:, 2].max()
    under = co[co[:, 2] <= height * spec["hull_top"] + 1e-6]
    pts = farthest_points(under, HULL_POINTS)
    hull_mesh = bpy.data.meshes.new("UCX_%s_00" % obj.name)
    bm = bmesh.new()
    for p in pts:
        bm.verts.new(p)
    bmesh.ops.convex_hull(bm, input=bm.verts)
    bm.to_mesh(hull_mesh)
    bm.free()
    hull = bpy.data.objects.new("UCX_%s_00" % obj.name, hull_mesh)
    scene.collection.objects.link(hull)
    assert hull.name == "UCX_%s_00" % obj.name, "%s: hull named %s" % (name, hull.name)

    # Textures, by role, straight from the GLB.
    material = obj.data.materials[0]
    images = {n.image.name.split("_")[0]: n.image for n in material.node_tree.nodes
              if n.type == "TEX_IMAGE" and n.image}
    written = {}
    for role, prefix in (("BaseColor", "Color"), ("ORM", "ORM"), ("Normal", "NormalGL")):
        image = images[prefix]
        w, h = image.size
        px = np.array(image.pixels[:], dtype=np.float32).reshape(h, w, 4)
        path = os.path.join(out, "T_Deposit_%s_%s.png" % (name, role))
        save_image(px, w, h, path)
        written[role] = os.path.basename(path)
        if role == "BaseColor" and spec["emissive"]:
            mask = emissive_mask(spec["emissive"], px)
            glow = np.zeros_like(px)
            glow[..., 3] = 1.0
            glow[mask, :3] = px[mask, :3]
            epath = os.path.join(out, "T_Deposit_%s_Emissive.png" % name)
            save_image(glow, w, h, epath)
            written["Emissive"] = os.path.basename(epath)
            spec["emissive_fraction"] = round(float(mask.mean()), 4)

    # Export the mesh and its hull together, as the greybox exports do.
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    hull.select_set(True)
    bpy.context.view_layer.objects.active = obj
    fbx = os.path.join(out, obj.name + ".fbx")
    bpy.ops.export_scene.fbx(
        filepath=fbx, use_selection=True, apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS", object_types={"MESH"},
        mesh_smooth_type="FACE", use_mesh_modifiers=True,
        bake_space_transform=False, axis_forward="-Z", axis_up="Y",
        path_mode="STRIP")

    tris = sum(len(p.vertices) - 2 for p in mesh.polygons)
    entry = {
        "item": spec["item"], "name": name, "mesh": obj.name, "fbx": os.path.relpath(fbx, ROOT).replace("\\", "/"),
        "textures": {k: os.path.join(name, v).replace("\\", "/") for k, v in written.items()},
        "metal_low": spec["metal_low"], "metal_high": spec["metal_high"],
        "rough_min": spec["rough_min"], "emissive_strength": spec["emissive_strength"],
        "triangles": tris, "hull_points": len(pts),
        "size_m": [round(float(v), 3) for v in (co.max(axis=0) - co.min(axis=0))],
    }
    if "emissive_fraction" in spec:
        entry["emissive_fraction"] = spec["emissive_fraction"]

    for o in (obj, hull):
        bpy.data.objects.remove(o)
    return entry


def main():
    # Its own scene, so whatever is open in an interactive Blender is left alone.
    previous = bpy.context.window.scene if bpy.context.window else None
    scene = bpy.data.scenes.new("DepositPrep")
    if bpy.context.window:
        bpy.context.window.scene = scene
    try:
        manifest = [prepare(dict(spec), scene) for spec in DEPOSITS]
    finally:
        if bpy.context.window and previous:
            bpy.context.window.scene = previous
        bpy.data.scenes.remove(scene)
    with open(os.path.join(ROOT, "deposits.json"), "w", encoding="utf-8") as f:
        json.dump({"$comment": "Written by tools/deposits/prepare_deposits.py (task 189); read by "
                               "tools/deposits/ue_import_deposits.py. Regenerate rather than edit.",
                   "deposits": manifest}, f, indent=2)
    for entry in manifest:
        print("DEPOSIT %(name)s: %(triangles)d triangles, hull %(hull_points)d points, size %(size_m)s m"
              % entry)
    return manifest


if __name__ == "__main__":
    main()
