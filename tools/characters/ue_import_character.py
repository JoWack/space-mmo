"""
Bring a character's mesh into Unreal on the shared mannequin skeleton, textured: task 177's first half.

    UnrealEditor-Cmd.exe client/SpaceMMO.uproject "-run=pythonscript" "-script=D:/Programming/SpaceMMO/tools/characters/ue_import_character.py"

Quote both arguments in PowerShell: Windows PowerShell splits an unquoted -name=a.b at the dot (176).
Close the editor first. This replaces assets the editor loads -- the textures, the mesh, the jump clip
-- and the editor keeps their files open, so its saves fail; a blocked save still leaves the change in
this process's memory, where every read-back agrees with it, so each save is checked on disk and one
that did not land fails the run (177). It does not touch the animation blueprint or the config. Pointing
the game at the mesh is a line in DefaultGame.ini.

For each character in CHARACTERS:
  * its four Tripo maps, from client/RawContent/Characters/<name>/, as textures: colour in sRGB, the
    rest linear, and the normal map's green flipped from OpenGL's convention, which Tripo writes, to
    DirectX's, which Unreal reads (180);
  * M_Character_Textured, one material every Tripo body shares, made once: four texture parameters
    wired straight to colour, normal, roughness and metallic, flagged for skeletal meshes;
  * MI_<name>, an instance of it with this character's maps;
  * SK_<name>, the FBX's mesh on SK_Mannequin -- the skeleton ABP_Human and every library clip play on
    (175) -- with a physics asset, its one material slot set to MI_<name>, and LODs Unreal generates
    (Joe chose the whole mesh as LOD0 and Unreal's LODs for the rest, 180).

Then it reads back what it saved and prints a CHAR line per check and a Result. The height is the
one the pawn measures (bounds, 125): it sizes the body by it, so a wrong scale shows as a multiplier.
"""

import os
import time

import unreal

PROJECT = "D:/Programming/SpaceMMO/client/"
SKELETON = "/Game/FreeAnimationLibrary/Demo/Characters/Mannequins/Meshes/SK_Mannequin"
SHARED = "/Game/Characters/Shared"
MASTER = "M_Character_Textured"

CHARACTERS = [
    # name, content folder, expected height in cm (Blender's read-back of the FBX: 1.8000 m)
    ("HumanoidMale", "/Game/Characters/Humanoid", 180.0),
]

# Bodies whose own normal map is not worn: their instance takes the engine's flat normal instead. The
# Humanoid man's drew a ragged strip along his hairline, scratches under his cuffs and blotches across his
# face in side light, and without it he is clean; Joe checked it in game with ShowFlag.MaterialNormal 0
# (177). The map is still imported, so a body can go back to it by leaving this set.
FLAT_NORMAL = "/Engine/EngineMaterials/FlatNormal"
FLAT_NORMAL_FOR = {"HumanoidMale"}

# Clips authored on a body rather than on the skeleton's reference pose. SK_Mannequin retargets some bones
# with AnimationScaled, the pelvis, spine_01 and the thigh twists among them: a clip's translations are
# scaled by the body playing it over the clip's retarget source, which defaults to the skeleton's
# reference -- the mannequin's proportions.
# A clip made at this body's proportions was scaled twice: the Humanoid man's thigh twist bones sat 30.2 cm
# down the thigh instead of 20.9 and spine_01 22.9 cm above the pelvis instead of 9.2, which Joe saw as his
# hips stretching in the jump (177).
AUTHORED_ON = {
    "HumanoidMale": ["/Game/Characters/Humanoid/anim_HumanoidMale_JumpStart"],
}
LOD_COUNT = 4

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
problems = []
STARTED = time.time()


def say(msg):
    unreal.log("CHAR: " + msg)


def fail(msg):
    problems.append(msg)
    unreal.log_error("CHAR: " + msg)


def save(asset):
    """Saves, and proves the file was written this run. With the editor open, the retarget source was set,
    the save failed on a locked file, and the read-back -- of this process's memory -- said it had worked."""
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
    paths = task.get_editor_property("imported_object_paths")
    return [unreal.load_asset(p) for p in paths]


def import_textures(name, folder):
    roles = {
        "BaseColor": (True, unreal.TextureCompressionSettings.TC_DEFAULT, False),
        "Normal": (False, unreal.TextureCompressionSettings.TC_NORMALMAP, True),
        "Roughness": (False, unreal.TextureCompressionSettings.TC_MASKS, False),
        "Metallic": (False, unreal.TextureCompressionSettings.TC_MASKS, False),
    }
    textures = {}
    for role, (srgb, compression, flip_green) in roles.items():
        asset = "T_%s_%s" % (name, role)
        objects = import_file("%sRawContent/Characters/%s/%s.png" % (PROJECT, name, asset), folder + "/Textures", asset)
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
    mat.set_editor_property("used_with_skeletal_mesh", True)
    wiring = [
        ("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "RGB", unreal.MaterialProperty.MP_BASE_COLOR, -500),
        ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "RGB", unreal.MaterialProperty.MP_NORMAL, -150),
        ("Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "R", unreal.MaterialProperty.MP_ROUGHNESS, 200),
        ("Metallic", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "R", unreal.MaterialProperty.MP_METALLIC, 550),
    ]
    for role, sampler, output, prop, y in wiring:
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -450, y)
        node.set_editor_property("parameter_name", role)
        node.set_editor_property("sampler_type", sampler)
        # The defaults are the first character's maps, which match each sampler type exactly; an
        # engine default in the wrong colour space is a compile error.
        node.set_editor_property("texture", textures[role])
        if not mel.connect_material_property(node, output, prop):
            fail("%s did not wire to %s" % (role, prop))
    mel.recompile_material(mat)
    save(mat)
    say("made %s" % path)
    return mat


def material_instance(name, folder, master, textures):
    asset = "MI_%s" % name
    path = "%s/%s" % (folder, asset)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = tools.create_asset(asset, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for role, tex in textures.items():
        if role == "Normal" and name in FLAT_NORMAL_FOR:
            tex = unreal.load_asset(FLAT_NORMAL)
            if tex is None:
                fail("no %s for %s to wear" % (FLAT_NORMAL, asset))
                continue
        mel.set_material_instance_texture_parameter_value(mi, role, tex)
    mel.update_material_instance(mi)
    save(mi)
    return mi


def import_mesh(name, folder, skeleton):
    # Interchange is this engine's default for FBX and ignores FbxImportUI (168).
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("create_physics_asset", True)
    ui.set_editor_property("skeleton", skeleton)
    ui.set_editor_property("automated_import_should_detect_type", False)
    data = ui.get_editor_property("skeletal_mesh_import_data")
    data.set_editor_property("convert_scene", True)
    # Auto-Rig Pro writes centimetres, which this converts to nothing; the height check proves it (176).
    data.set_editor_property("convert_scene_unit", True)
    data.set_editor_property("import_morph_targets", False)
    data.set_editor_property("update_skeleton_reference_pose", False)
    objects = import_file("%sRawContent/Characters/%s/%s.fbx" % (PROJECT, name, name), folder, "SK_%s" % name, ui)
    return next((o for o in objects if isinstance(o, unreal.SkeletalMesh)), None)


def main():
    skeleton = unreal.load_asset(SKELETON)
    if skeleton is None:
        fail("no skeleton at %s" % SKELETON)
        return
    for name, folder, height in CHARACTERS:
        say("%s into %s" % (name, folder))
        textures = import_textures(name, folder)
        if len(textures) != 4:
            continue
        master = master_material(textures)
        mi = material_instance(name, folder, master, textures)
        mesh = import_mesh(name, folder, skeleton)
        if mesh is None:
            fail("SK_%s did not import" % name)
            continue
        # Indexing the array hands back a copy of the struct, so each one is set and written back; setting
        # them in a for loop changed copies and saved the slots unchanged (first run, 4 October).
        slots = mesh.get_editor_property("materials")
        for i in range(len(slots)):
            slot = slots[i]
            slot.set_editor_property("material_interface", mi)
            slots[i] = slot
        mesh.set_editor_property("materials", slots)
        meshes = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
        if not meshes.regenerate_lod(mesh, LOD_COUNT, False, False):
            fail("SK_%s: LOD generation failed" % name)
        # Only what this made: saving the folder re-saved the jump start beside it (first run).
        save(mesh)
        physics = mesh.get_editor_property("physics_asset")
        if physics is not None:
            save(physics)
        for clip_path in AUTHORED_ON.get(name, []):
            clip = unreal.load_asset(clip_path)
            if clip is None:
                fail("%s is not there to take %s as its retarget source" % (clip_path, mesh.get_name()))
                continue
            # SPACEMMO_PROVE_CLIP_CHECK=1 clears the source instead, so the check below can be seen to fail
            # against the bug it exists for; a normal run sets it back.
            source = None if os.environ.get("SPACEMMO_PROVE_CLIP_CHECK") == "1" else mesh
            clip.set_editor_property("retarget_source_asset", source)
            save(clip)
        check(name, folder, height, skeleton, mi)
        check_normal(name, mi)
        check_clips(name, mesh)


def check(name, folder, height, skeleton, mi):
    """Reads back what was saved: the skeleton, the height the pawn will measure, the material, the LODs."""
    path = "%s/SK_%s" % (folder, name)
    mesh = unreal.load_asset(path)
    if mesh is None:
        fail("%s is not there after saving" % path)
        return
    meshes = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    on = mesh.get_editor_property("skeleton")
    bounds = mesh.get_bounds()
    measured = bounds.box_extent.z * 2.0
    lods = meshes.get_lod_count(mesh)
    verts = [meshes.get_num_verts(mesh, i) for i in range(lods)]
    slots = [s.get_editor_property("material_interface") for s in mesh.get_editor_property("materials")]
    physics = mesh.get_editor_property("physics_asset")
    say("%s: skeleton %s; height %.1f cm (expected %.1f); %d LODs, vertices %s; material %s; physics %s"
        % (path, on.get_path_name() if on else None, measured, height, lods, verts,
           [s.get_name() if s else None for s in slots], physics.get_name() if physics else None))
    if on is None or on.get_path_name() != skeleton.get_path_name():
        fail("%s is not on %s" % (path, SKELETON))
    if abs(measured - height) > 2.0:
        fail("%s is %.1f cm tall, not %.1f: the scale did not come through" % (path, measured, height))
    if lods != LOD_COUNT or any(b >= a for a, b in zip(verts, verts[1:])):
        fail("%s's LODs are not %d, each lighter than the last" % (path, LOD_COUNT))
    if not slots or any(s is None or s.get_path_name() != mi.get_path_name() for s in slots):
        fail("%s does not wear MI_%s" % (path, name))
    if physics is None:
        fail("%s has no physics asset" % path)


def check_normal(name, mi):
    """The normal the instance wears: the engine's flat one for a body in FLAT_NORMAL_FOR, its own map otherwise."""
    worn = mel.get_material_instance_texture_parameter_value(mi, "Normal")
    want = FLAT_NORMAL if name in FLAT_NORMAL_FOR else "%s/Textures/T_%s_Normal" % (mi.get_path_name().rsplit("/", 1)[0], name)
    worn_path = worn.get_path_name().split(".")[0] if worn else None
    say("MI_%s wears normal %s (expected %s)" % (name, worn_path, want))
    if worn_path != want:
        fail("MI_%s wears normal %s, not %s" % (name, worn_path, want))


def check_clips(name, mesh):
    """A clip authored on this body has to play on it as authored: retargeting it onto the body it was made
    on must move no bone. Every bone's offset from its parent is compared, every frame, retargeted and not.
    Scaled twice, the thigh twist bones sat 30.2 cm down the thigh instead of 20.9 and spine_01 22.9 cm
    above the pelvis instead of 9.2."""
    poses = unreal.AnimPoseExtensions

    # A clip made on this body that nobody listed would play scaled twice and pass, unchecked. 176 names
    # them anim_<body>_<clip>, beside the body; any such clip missing from AUTHORED_ON fails the run.
    folder = mesh.get_path_name().rsplit("/", 1)[0]
    listed = set(AUTHORED_ON.get(name, []))
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=False, include_folder=False):
        package = str(path).split(".")[0]
        asset = unreal.load_asset(package)
        if (isinstance(asset, unreal.AnimSequence) and asset.get_name().startswith("anim_%s_" % name)
                and package not in listed):
            fail("%s was made on %s by its name, and is not in AUTHORED_ON" % (package, mesh.get_name()))

    def offsets(anim, frame, retarget):
        opts = unreal.AnimPoseEvaluationOptions()
        opts.set_editor_property("optional_skeletal_mesh", mesh)
        opts.set_editor_property("should_retarget", retarget)
        opts.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
        pose = poses.get_anim_pose_at_frame(anim, frame, opts)
        return {str(b): poses.get_bone_pose(pose, b, unreal.AnimPoseSpaces.LOCAL).translation.length()
                for b in poses.get_bone_names(pose)}

    for clip_path in AUTHORED_ON.get(name, []):
        clip = unreal.load_asset(clip_path)
        frames = unreal.AnimationLibrary.get_num_frames(clip) if clip else 0
        if frames == 0:
            fail("%s has no frames to check" % clip_path)
            continue
        worst, at = 0.0, None
        bones = 0
        for f in range(frames + 1):          # frames counts the intervals; the keys are one more
            played, authored = offsets(clip, f, True), offsets(clip, f, False)
            bones = len(played)
            for bone, length in played.items():
                if abs(length - authored[bone]) > worst:
                    worst, at = abs(length - authored[bone]), (bone, f, length, authored[bone])
        where = "; worst %s at key %d, %.1f cm from its parent against %.1f authored" % at if at else ""
        say("%s on %s: %d bones over %d keys move up to %.2f cm when retargeted%s; retarget source %s"
            % (clip.get_name(), mesh.get_name(), bones, frames + 1, worst, where,
               clip.get_editor_property("retarget_source_asset")))
        if bones == 0 or worst > 0.5:
            fail("%s does not play on %s as authored: check its retarget source" % (clip.get_name(), mesh.get_name()))


main()
say("%d problem(s). Result: %s" % (len(problems), "OK" if not problems else "FAILED"))
