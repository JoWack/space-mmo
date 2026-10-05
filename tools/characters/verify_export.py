#!/usr/bin/env python3
"""
Read a character's exported FBX back in a clean Blender and check it is what Unreal's SK_Mannequin can take
(175, 182): what the export says, not what the scene that made it says.

    blender --factory-startup --background --python-exit-code 1 --python tools/characters/verify_export.py -- \
        --fbx client/RawContent/Characters/NAME/NAME.fbx --height M

  * every bone is a bone SK_Mannequin has, so importing adds nothing to the shared skeleton (125 once added
    face bones that way);
  * the height is --height to the millimetre, and every vertex group is a bone;
  * the clip's root, the armature object, does not move, and the pelvis's height range is printed: the clip
    importer checks it (SpaceMMOImportClipsCommandlet's PelvisMinCm and PelvisMaxCm).
Prints VERIFY lines and a Result; exits 1 on any failure.
"""

import sys

import bpy
import numpy as np

SK_MANNEQUIN = set("""ball_l ball_r calf_l calf_r calf_twist_01_l calf_twist_01_r calf_twist_02_l calf_twist_02_r center_of_mass
clavicle_l clavicle_r foot_l foot_r hand_l hand_r head ik_foot_l ik_foot_r ik_foot_root ik_hand_gun ik_hand_l ik_hand_r
ik_hand_root index_01_l index_01_r index_02_l index_02_r index_03_l index_03_r index_metacarpal_l index_metacarpal_r
interaction lowerarm_l lowerarm_r lowerarm_twist_01_l lowerarm_twist_01_r lowerarm_twist_02_l lowerarm_twist_02_r
middle_01_l middle_01_r middle_02_l middle_02_r middle_03_l middle_03_r middle_metacarpal_l middle_metacarpal_r neck_01
neck_02 pelvis pinky_01_l pinky_01_r pinky_02_l pinky_02_r pinky_03_l pinky_03_r pinky_metacarpal_l pinky_metacarpal_r
ring_01_l ring_01_r ring_02_l ring_02_r ring_03_l ring_03_r ring_metacarpal_l ring_metacarpal_r root spine_01 spine_02
spine_03 spine_04 spine_05 thigh_l thigh_r thigh_twist_01_l thigh_twist_01_r thigh_twist_02_l thigh_twist_02_r thumb_01_l
thumb_01_r thumb_02_l thumb_02_r thumb_03_l thumb_03_r upperarm_l upperarm_r upperarm_twist_01_l upperarm_twist_01_r
upperarm_twist_02_l upperarm_twist_02_r""".split())


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    fbx = argv[argv.index("--fbx") + 1]
    height = float(argv[argv.index("--height") + 1])
    problems = []

    def say(msg):
        print("VERIFY " + msg)

    def fail(msg):
        problems.append(msg)
        print("VERIFY FAIL " + msg)

    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=fbx)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    mesh = next(o for o in bpy.data.objects if o.type == "MESH")
    bones = [b.name for b in arm.data.bones]
    extra = sorted(b for b in bones if b not in SK_MANNEQUIN)
    say("%d bones, %d deforming groups; not in SK_Mannequin: %s" % (len(bones), len(mesh.vertex_groups), extra))
    if extra:
        fail("bones SK_Mannequin does not have: %s" % extra)
    co = np.array([mesh.matrix_world @ v.co for v in mesh.data.vertices])
    h = float(co[:, 2].max() - co[:, 2].min())
    say("height %.4f m (expected %.4f), soles at %.4f" % (h, height, co[:, 2].min()))
    if abs(h - height) > 0.001:
        fail("height %.4f m, not %.4f" % (h, height))
    groups = {g.name for g in mesh.vertex_groups}
    if not groups <= set(bones):
        fail("vertex groups that are not bones: %s" % sorted(groups - set(bones))[:10])
    acts = [(a.name, tuple(a.frame_range)) for a in bpy.data.actions]
    say("actions %s" % acts)
    if not (arm.animation_data and arm.animation_data.action):
        fail("no clip on the armature")
    else:
        a = arm.animation_data.action
        f0, f1 = int(a.frame_range[0]), int(a.frame_range[1])
        sc = bpy.context.scene
        roots, pelvis = [], []
        for f in range(f0, f1 + 1):
            sc.frame_set(f)
            roots.append(tuple(arm.matrix_world.translation))
            pelvis.append((arm.matrix_world @ arm.pose.bones["pelvis"].head).z)
        drift = float(np.ptp(np.array(roots), 0).max())
        say("clip frames %d..%d (%d keys) at %s fps; root moves %.4f m; pelvis %.1f to %.1f cm"
            % (f0, f1, f1 - f0 + 1, sc.render.fps, drift, 100 * min(pelvis), 100 * max(pelvis)))
        if drift > 1e-4:
            fail("the root moves %.4f m" % drift)
        if sc.render.fps / sc.render.fps_base != 30:
            # Clips are authored at 30 a second (176). The Martian's first export was at Blender's default 24,
            # which plays his jump start a third slower (182).
            fail("the clip is at %s fps, not 30" % (sc.render.fps / sc.render.fps_base))
    say("%d problem(s). Result: %s" % (len(problems), "OK" if not problems else "FAILED"))
    if problems:
        sys.exit(1)


main()
