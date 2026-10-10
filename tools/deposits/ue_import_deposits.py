"""
Bring the five resource deposits into Unreal, textured and solid: task 189.

    UnrealEditor-Cmd.exe client/SpaceMMO.uproject "-run=pythonscript" "-script=D:/Programming/SpaceMMO/tools/deposits/ue_import_deposits.py"

Quote both arguments in PowerShell, which splits an unquoted -name=a.b at the dot (176). Close the editor
first: it holds the files of whatever it has loaded, a blocked save still changes this process's memory,
and every read-back then agrees with it (177). So each save is checked on disk.

Reads client/RawContent/Deposits/deposits.json, which tools/deposits/prepare_deposits.py writes, and for
each deposit makes, under /Game/Deposits/<Name>/:
  * its textures: colour and emissive in sRGB, ORM linear as masks, and the normal map's green flipped
    from OpenGL's convention, which Tripo writes, to DirectX's, which Unreal reads (180);
  * MI_Deposit_<Name>, an instance of M_Deposit_Textured with this deposit's maps and settings;
  * SM_Deposit_<Name>, the FBX's mesh with its UCX_ hull as simple collision and the instance on its slot.

M_Deposit_Textured is made once, in /Game/Deposits/Shared, and kept if it exists. ORM's occlusion goes
straight to ambient occlusion; its roughness is floored at RoughMin; its metallic goes through a smoothstep
from MetalLow to MetalHigh, so only the brightest of it stays metal -- Tripo painted stone and paint half
metallic and they read as oil (189). Emissive is a texture times EmissiveStrength, black and zero for a
deposit that does not glow.

Pointing the game at the meshes is the Meshes line in DefaultGame.ini. Prints a DEPOSIT line per check
and a Result.
"""

import json
import os
import time

import unreal

PROJECT = "D:/Programming/SpaceMMO/client/"
RAW = PROJECT + "RawContent/Deposits/"
SHARED = "/Game/Deposits/Shared"
MASTER = "M_Deposit_Textured"
BLACK = "/Engine/EngineResources/Black"

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
problems = []
STARTED = time.time()


def say(msg):
    unreal.log("DEPOSIT: " + msg)


def fail(msg):
    problems.append(msg)
    unreal.log_error("DEPOSIT: " + msg)


def save(asset):
    """Saves, and proves the file was written by this run."""
    package = asset.get_path_name().split(".")[0]
    filename = PROJECT + "Content/" + package[len("/Game/"):] + ".uasset"
    saved = unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    if not saved or not os.path.exists(filename) or os.path.getmtime(filename) < STARTED:
        fail("%s did not reach the disk: is the editor open? It holds the files of what it has loaded" % package)


def import_file(filename, folder, name, options=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    if options is not None:
        task.set_editor_property("options", options)
    tools.import_asset_tasks([task])
    return [unreal.load_asset(p) for p in task.get_editor_property("imported_object_paths")]


ROLES = {
    # role: (sRGB, compression, flip green)
    "BaseColor": (True, unreal.TextureCompressionSettings.TC_DEFAULT, False),
    "Normal": (False, unreal.TextureCompressionSettings.TC_NORMALMAP, True),
    "ORM": (False, unreal.TextureCompressionSettings.TC_MASKS, False),
    "Emissive": (True, unreal.TextureCompressionSettings.TC_DEFAULT, False),
}


def import_textures(entry, folder):
    textures = {}
    for role, relative in entry["textures"].items():
        srgb, compression, flip_green = ROLES[role]
        asset = os.path.splitext(os.path.basename(relative))[0]
        objects = import_file(RAW + relative, folder + "/Textures", asset)
        tex = next((o for o in objects if isinstance(o, unreal.Texture2D)), None)
        if tex is None:
            fail("%s did not import" % asset)
            continue
        tex.set_editor_property("srgb", srgb)
        tex.set_editor_property("compression_settings", compression)
        tex.set_editor_property("flip_green_channel", flip_green)
        save(tex)
        textures[role] = tex
    return textures


def master_material(textures):
    path = "%s/%s" % (SHARED, MASTER)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        say("%s exists; kept as it is" % path)
        return unreal.load_asset(path)
    mat = tools.create_asset(MASTER, SHARED, unreal.Material, unreal.MaterialFactoryNew())

    def sampler(role, kind, x, y, default):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", role)
        node.set_editor_property("sampler_type", kind)
        # A default in the sampler's own colour space, or the material does not compile.
        node.set_editor_property("texture", default)
        return node

    def scalar(name, value, x, y):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        return node

    S = unreal.MaterialSamplerType
    P = unreal.MaterialProperty
    colour = sampler("BaseColor", S.SAMPLERTYPE_COLOR, -900, -600, textures["BaseColor"])
    normal = sampler("Normal", S.SAMPLERTYPE_NORMAL, -900, -250, textures["Normal"])
    orm = sampler("ORM", S.SAMPLERTYPE_MASKS, -900, 100, textures["ORM"])
    glow = sampler("Emissive", S.SAMPLERTYPE_COLOR, -900, 600, unreal.load_asset(BLACK))

    wired = [
        mel.connect_material_property(colour, "RGB", P.MP_BASE_COLOR),
        mel.connect_material_property(normal, "RGB", P.MP_NORMAL),
        mel.connect_material_property(orm, "R", P.MP_AMBIENT_OCCLUSION),
    ]

    rough_min = scalar("RoughMin", 0.35, -600, 250)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionMax, -350, 200)
    wired += [mel.connect_material_expressions(orm, "G", rough, "A"),
              mel.connect_material_expressions(rough_min, "", rough, "B"),
              mel.connect_material_property(rough, "", P.MP_ROUGHNESS)]

    metal_low = scalar("MetalLow", 0.55, -600, 380)
    metal_high = scalar("MetalHigh", 0.80, -600, 460)
    metal = mel.create_material_expression(mat, unreal.MaterialExpressionSmoothStep, -350, 400)
    wired += [mel.connect_material_expressions(metal_low, "", metal, "Min"),
              mel.connect_material_expressions(metal_high, "", metal, "Max"),
              mel.connect_material_expressions(orm, "B", metal, "Value"),
              mel.connect_material_property(metal, "", P.MP_METALLIC)]

    strength = scalar("EmissiveStrength", 0.0, -600, 750)
    emissive = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, 650)
    wired += [mel.connect_material_expressions(glow, "RGB", emissive, "A"),
              mel.connect_material_expressions(strength, "", emissive, "B"),
              mel.connect_material_property(emissive, "", P.MP_EMISSIVE_COLOR)]

    if not all(wired):
        fail("%s: %d of %d connections failed" % (MASTER, wired.count(False), len(wired)))
    mel.recompile_material(mat)
    save(mat)
    say("made %s" % path)
    return mat


def material_instance(entry, folder, master, textures):
    asset = "MI_Deposit_%s" % entry["name"]
    path = "%s/%s" % (folder, asset)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = tools.create_asset(asset, folder, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for role, tex in textures.items():
        mel.set_material_instance_texture_parameter_value(mi, role, tex)
    for name, key in (("RoughMin", "rough_min"), ("MetalLow", "metal_low"), ("MetalHigh", "metal_high"),
                      ("EmissiveStrength", "emissive_strength")):
        mel.set_material_instance_scalar_parameter_value(mi, name, float(entry[key]))
    mel.update_material_instance(mi)
    save(mi)
    return mi


def import_mesh(entry, folder):
    # Interchange is this engine's default for FBX and ignores FbxImportUI (168).
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("convert_scene", True)
    # Blender writes metres and says so; without this the mesh arrives a hundredth of its size (168).
    data.set_editor_property("convert_scene_unit", True)
    data.set_editor_property("combine_meshes", True)
    # The UCX_ hull is the collision; Unreal's own would be a box.
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("one_convex_hull_per_ucx", True)
    data.set_editor_property("generate_lightmap_u_vs", False)
    data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    objects = import_file(RAW + entry["fbx"], folder, entry["mesh"], ui)
    return next((o for o in objects if isinstance(o, unreal.StaticMesh)), None)


def check(entry, mesh, mi):
    name = entry["name"]
    # Read off the body setup: the static-mesh editor subsystem is None in a commandlet.
    body = mesh.get_editor_property("body_setup")
    geom = body.get_editor_property("agg_geom") if body else None
    hulls = len(geom.get_editor_property("convex_elems")) if geom else 0
    if hulls < 1:
        fail("%s has no simple collision: nothing will bump into it (130)" % name)
    material = mesh.get_material(0)
    if material is None or material.get_path_name() != mi.get_path_name():
        fail("%s wears %s, not %s" % (name, material.get_path_name() if material else None, mi.get_name()))
    box = mesh.get_bounding_box()
    size_cm = [round(box.max.x - box.min.x, 1), round(box.max.y - box.min.y, 1), round(box.max.z - box.min.z, 1)]
    expected = [round(v * 100.0, 1) for v in entry["size_m"]]
    # Blender's -Z forward, Y up: the width axes may swap, the height may not.
    if abs(size_cm[2] - expected[2]) > 2.0 or abs(sorted(size_cm[:2])[1] - sorted(expected[:2])[1]) > 2.0:
        fail("%s is %s cm; Blender wrote %s cm" % (name, size_cm, expected))
    say("%s: %d hull(s), %s cm, wearing %s, %d vertices"
        % (name, hulls, size_cm, mi.get_name(), mesh.get_num_vertices(0)))


def main():
    manifest = json.load(open(RAW + "deposits.json", encoding="utf-8"))["deposits"]
    master = None
    for entry in manifest:
        folder = "/Game/Deposits/%s" % entry["name"]
        textures = import_textures(entry, folder)
        if master is None:
            master = master_material(textures)
        mi = material_instance(entry, folder, master, textures)
        mesh = import_mesh(entry, folder)
        if mesh is None:
            fail("%s did not import" % entry["mesh"])
            continue
        mesh.set_material(0, mi)
        save(mesh)
        check(entry, mesh, mi)
    say("Result: %s" % ("OK" if not problems else "FAILED (%d)" % len(problems)))
    for p in problems:
        say("  " + p)


main()
