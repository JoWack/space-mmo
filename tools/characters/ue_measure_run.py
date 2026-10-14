"""
The run every race plays, measured on each body as the game plays it, for the sprint to follow sideways (task 191).

    UnrealEditor-Cmd.exe client/SpaceMMO.uproject "-run=pythonscript" "-script=D:/Programming/SpaceMMO/tools/characters/ue_measure_run.py"

Joe, 13 October, after playing the sprints: going from the run to the sprint "the arms and legs move much differently
outward/inward". Measured, the library's jog swings each hand across the chest at the front of its stride and out past
the hip at the back, and flares the knees as each leg swings through; the first sprints swung the arms straight and
held the knees in line, so every blend between the two dragged hands and knees sideways, up to 29 cm on the orc.

ABP_Human's blend space keeps its samples in step by footfall (the clip importer's sync markers, in its sync group),
and the jog and every sprint hold two strides with each foot landing at nearly the same fractions (191), so at any
fraction both are at nearly the same point of the stride. This writes where the run puts the elbows, hands, knees and ankles at each fraction, on each body, retargeted
as the game retargets it, to tools/characters/run_shape.json; rig_character.py's sprint() puts the sprint's joints at
the same distances from the body's midline at the same fractions, and keeps its own fore-aft and vertical motion.

Read-only: it loads the clips and meshes and saves nothing, so it can run beside an open editor.
Positions are the joint relative to the root bone in the body's own space, centimetres: x across the body (left
positive), y forward, z up -- the same axes as the Blender rig's, whose forward is -y.
"""

import json
import os

import unreal

RUN = "/Game/FreeAnimationLibrary/Animations/Jog/anim_Jog_Loop_Fwd"
BODIES = (("HumanoidMale", "Humanoid"), ("MartianMale", "Martian"), ("SpaceElfMale", "SpaceElf"),
          ("SpaceOrcMale", "SpaceOrc"))
JOINTS = ("upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball")
OUT = os.path.join(os.path.dirname(__file__) if "__file__" in globals() else "D:/Programming/SpaceMMO/tools/characters",
                   "run_shape.json")
poses = unreal.AnimPoseExtensions


def say(msg):
    unreal.log("RUNSHAPE: " + msg)


def measure(anim, mesh):
    keys = unreal.AnimationLibrary.get_num_frames(anim)
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property("optional_skeletal_mesh", mesh)
    opts.set_editor_property("should_retarget", True)
    opts.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    tracks = {"%s_%s" % (j, s): [] for j in JOINTS for s in ("l", "r")}
    for f in range(keys + 1):
        pose = poses.get_anim_pose_at_frame(anim, f, opts)
        root = poses.get_bone_pose(pose, "root", unreal.AnimPoseSpaces.WORLD).translation
        for bone in tracks:
            t = poses.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
            tracks[bone].append([round(t.x - root.x, 2), round(t.y - root.y, 2), round(t.z - root.z, 2)])
    return keys, tracks


out = {"clip": RUN, "units": "cm, root-relative, x across (left +), y forward, z up", "bodies": {}}
anim = unreal.load_asset(RUN)
for body, folder in BODIES:
    mesh = unreal.load_asset("/Game/Characters/%s/SK_%s" % (folder, body))
    keys, tracks = measure(anim, mesh)
    out["bodies"][body] = {"keys": keys + 1, "seconds": round(anim.get_play_length(), 4), "tracks": tracks}
    hx = [p[0] for p in tracks["hand_l"]]
    say("%s: %d keys; left hand %.1f to %.1f cm across, left knee %.1f to %.1f" % (
        body, keys + 1, min(hx), max(hx), min(p[0] for p in tracks["calf_l"]), max(p[0] for p in tracks["calf_l"])))
with open(OUT, "w") as f:
    json.dump(out, f, indent=1)
say("wrote %s" % OUT)
