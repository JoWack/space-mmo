#!/usr/bin/env python3
"""
Read a character's exported FBX back in a clean Blender and check it is what Unreal's SK_Mannequin can take
(175, 182): what the export says, not what the scene that made it says.

    blender --factory-startup --background --python-exit-code 1 --python tools/characters/verify_export.py -- \
        --fbx client/RawContent/Characters/NAME/NAME.fbx --height M [--fps 30] [--loop-speed M_PER_S --planted N]

  * every bone is a bone SK_Mannequin has, so importing adds nothing to the shared skeleton (125 once added
    face bones that way);
  * the height is --height to the millimetre, and every vertex group is a bone;
  * the clip's root, the armature object, does not move, and the pelvis's height range is printed: the clip
    importer checks it (SpaceMMOImportClipsCommandlet's PelvisMinCm and PelvisMaxCm).
With --loop-speed, for an in-place cycle such as the sprint (191): its last frame is its first, so it loops, and
each ball of the foot, while on the ground, slides back at that speed within 5% over N frame-to-frame steps in all
(176's check that matters, read off the file Unreal will import rather than the scene that made it). Halfway
between those keys it must not sink more than 1.5 cm below them: an engine interpolates the joints' rotations
there, not the foot's path, and at 30 keys a second the Humanoid man's sprint sank 4.4 cm (191). --fps is the rate
the clip must be keyed at: 30 for every clip but the sprints, which are 60 for that reason.
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


def loop_and_slide(arm, f0, f1, speed, planted_expected, fps, say, fail):
    """The loop closes, each planted ball slides back at `speed` m/s, and halfway between planted keys it does not
    sink into the ground."""
    sc = bpy.context.scene
    deform = [b for b in arm.pose.bones if b.bone.use_deform]
    balls = {"ball_l": [], "ball_r": []}
    first = last = None
    for f in range(f0, f1 + 1):
        sc.frame_set(f)
        mats = np.array([np.array(arm.matrix_world @ b.matrix) for b in deform])
        first = mats if first is None else first
        last = mats
        for name in balls:
            balls[name].append(np.array(arm.matrix_world @ arm.pose.bones[name].head))
    gap = float(np.abs(first[:, :3, 3] - last[:, :3, 3]).max())
    say("loop: the last frame's bones are %.4f m from the first's" % gap)
    if gap > 0.001:
        fail("the clip does not loop: its last frame is %.4f m from its first" % gap)
    steps = []
    for name, track in balls.items():
        b = np.array(track)
        low = b[:, 2].min()
        down = [i for i in range(len(b)) if b[i][2] - low < 0.002]
        for i, j in zip(down, down[1:]):
            if j == i + 1:
                d = b[j] - b[i]
                steps.append((name, f0 + i, float(np.hypot(d[0], d[1])) * fps))
    bad = [s for s in steps if abs(s[2] - speed) > 0.05 * speed]
    say("planted: %d steps, sliding %.2f to %.2f m/s (expected %d at %.2f)"
        % (len(steps), min(s[2] for s in steps) if steps else 0, max(s[2] for s in steps) if steps else 0,
           planted_expected, speed))
    if len(steps) != planted_expected or bad:
        fail("planted feet: %d steps (expected %d), off speed: %s" % (len(steps), planted_expected, bad[:4]))
    # Halfway between each pair of planted keys, where the file's interpolation puts the ball rather than the IK.
    sag, worst = 0.0, None
    for name, frame, _ in steps:
        planted_z = float(np.array(balls[name])[frame - f0][2])
        sc.frame_set(frame, subframe=0.5)
        sank = planted_z - float((arm.matrix_world @ arm.pose.bones[name].head).z)
        if sank > sag:
            sag, worst = sank, (name, frame)
    sc.frame_set(f0)
    say("planted: halfway between keys the ball sinks at most %.2f cm below them (%s, after frame %s)"
        % (100 * sag, worst[0] if worst else "-", worst[1] if worst else "-"))
    if sag > 0.015:
        fail("a planted ball sinks %.2f cm into the ground between keys: key the clip faster" % (100 * sag))


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
        fps = float(argv[argv.index("--fps") + 1]) if "--fps" in argv else 30.0
        if sc.render.fps / sc.render.fps_base != fps:
            # Clips are authored at 30 a second (176), the sprints at 60 (191). The Martian's first export was at
            # Blender's default 24, which plays his jump start a third slower (182).
            fail("the clip is at %s fps, not %s" % (sc.render.fps / sc.render.fps_base, fps))
        if "--loop-speed" in argv:
            loop_and_slide(arm, f0, f1, float(argv[argv.index("--loop-speed") + 1]),
                           int(argv[argv.index("--planted") + 1]), fps, say, fail)
    say("%d problem(s). Result: %s" % (len(problems), "OK" if not problems else "FAILED"))
    if problems:
        sys.exit(1)


main()
