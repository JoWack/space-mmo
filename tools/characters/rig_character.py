"""
Rig, skin, animate and export a body the way the Humanoid man was (tasks 175, 176, 182), in Blender's UI
session through the MCP: Auto-Rig Pro's Smart detection needs a 3D view, so this cannot run in the background.

    exec(open("D:/Programming/SpaceMMO/tools/characters/rig_character.py", encoding="utf-8").read())
    result = smart("MartianMale", marks)      # and so on, one step per MCP call, checking each result

`marks` is the JSON measure_landmarks.py prints for the body. Steps, in order, each returning a dict:
  1. smart(name, marks)   Smart's markers at the measured landmarks, then detection.
  2. limbs()              six spine bones (its root plus five) and two neck bones: what SK_Mannequin needs (175).
  3. bind(name)           match to the reference, bind with pseudo-voxels, normalise.
  4. weights(name, marks) 175's two corrections, at this body's armpit, torso edge and shoulder joint.
  5. check(name)          every vertex weighted, at most 8 influences, every sum 1.
  6. jump_start(marks)    the jump start, built as the Humanoid man's was, its distances scaled by leg length.
  7. export(name)         Auto-Rig Pro's FBX with the settings 175 recorded, and the four textures.

The sprint (191) is made on a finished rig, from its file, with nothing measured from markers:
    sprint(name)                      the Sprint action, keyed every frame from GAITS[name] and the run's shape
                                      across the body (run_shape.json, from ue_measure_run.py), and its measurements
    sprint_preview(name, out_dir)     key poses side and front, and a video of it running over a moving floor
    export(name, "Sprint", name + "_Sprint", maps=False, fps=SPRINT_FPS)

Every number that was the Humanoid man's is kept as his, with the scale it was taken at: k is the body's height
over his 1.80 m, kl its root height over his 0.92 m.
"""

import contextlib
import io
import json
import math
import os
import time

import bpy
import numpy as np
from mathutils import Matrix, Vector

HUMAN_HEIGHT, HUMAN_ROOT = 1.80, 0.92
RAW = "D:/Programming/SpaceMMO/client/RawContent/Characters/"


def _view3d():
    win = bpy.context.window_manager.windows[0]
    area = next(a for a in win.screen.areas if a.type == "VIEW_3D")
    region = next(r for r in area.regions if r.type == "WINDOW")
    return bpy.context.temp_override(window=win, area=area, region=region, space_data=area.spaces.active)


def _object_mode():
    if bpy.context.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")


def _select(*objs, active=None):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active or objs[-1]


def _flagged(log):
    return [l for l in log.splitlines() if any(k in l.lower() for k in ("error", "fail", "warning", "not found", "cannot"))][:10]


def smart(name, marks):
    body = bpy.data.objects[name]
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), _view3d():
        _object_mode()
        _select(body)
        got = list(bpy.ops.id.get_selected_objects("EXEC_DEFAULT"))
        placed = {}
        for part, loc in marks["markers"].items():
            bpy.ops.id.add_marker("EXEC_DEFAULT", body_part=part)
            m = bpy.data.objects[part + "_loc"]
            m.location = loc
            placed[part] = [round(v, 3) for v in m.location]
        bpy.context.view_layer.update()
        # Front, orthographic and framed on the body, as the Humanoid man's detection ran.
        space = bpy.context.space_data
        bpy.ops.view3d.view_axis(type="FRONT")
        space.region_3d.view_perspective = "ORTHO"
        temp = bpy.data.objects[bpy.context.scene.arp_body_name]
        frozen, temp.hide_select = temp.hide_select, False
        _select(temp)
        bpy.ops.view3d.view_selected()
        bpy.ops.object.select_all(action="DESELECT")
        temp.hide_select = frozen
        t0 = time.time()
        detect = list(bpy.ops.id.go_detect("EXEC_DEFAULT"))
    rig = bpy.data.objects.get("rig")
    names = [b.name for b in rig.data.bones] if rig else []
    log = buf.getvalue()
    # Detection reports FINISHED and leaves a half-made rig even when it fails: its log is the only sign.
    failed = "Error during detection? True" in log
    return {"get_selected": got, "arp_body": bpy.context.scene.arp_body_name, "markers": placed, "detect": detect,
            "failed": failed, "seconds": round(time.time() - t0, 1), "rig": bool(rig),
            "ref_bones": len([n for n in names if "_ref" in n]), "mode": bpy.context.mode,
            "flagged": _flagged(log), "log_tail": log[-600:] if failed else None}


def limbs():
    rig = bpy.data.objects["rig"]
    buf = io.StringIO()
    steps = {}
    with contextlib.redirect_stdout(buf), _view3d():
        _object_mode()
        _select(rig)
        # Straight after detection the reference bones are already showing, and edit_ref's poll refuses then;
        # after a match it is what shows them again (the Humanoid man's second pass, 175).
        if bpy.ops.arp.edit_ref.poll():
            steps["edit_ref"] = list(bpy.ops.arp.edit_ref("EXEC_DEFAULT"))
        else:
            bpy.ops.object.mode_set(mode="EDIT")
            steps["edit_ref"] = "references already showing"

        def pick(bone):
            bpy.ops.armature.select_all(action="DESELECT")
            eb = rig.data.edit_bones[bone]
            eb.select = eb.select_head = eb.select_tail = True
            rig.data.edit_bones.active = eb

        pick("spine_01_ref.x")
        steps["spine"] = list(bpy.ops.arp.show_limb_params(
            "EXEC_DEFAULT", limb_type="spine", spine_count=6, bottom=False, align_root_master=True,
            align_bend_controllers=True, spine_master=False, spine_master_space="LOCAL", spine_master_stretchy=False,
            spine_reverse=False, spine_update_vgroups=True, spine_preserve_shape=True, spine_parent_fallback="c_traj"))
        if bpy.context.mode != "EDIT_ARMATURE":
            bpy.ops.object.mode_set(mode="EDIT")
        pick("neck_ref.x")
        steps["neck"] = list(bpy.ops.arp.show_limb_params(
            "EXEC_DEFAULT", limb_type="neck", neck_count=2, neck_twist=False, neck_bendy=1, neck_parent_fallback="c_traj"))
    names = {b.name for b in rig.data.bones}
    steps.update({"spine_refs": sorted(n for n in names if n.startswith("spine_") and n.endswith("_ref.x")),
                  "neck_refs": sorted(n for n in names if "neck" in n and n.endswith("_ref.x")),
                  "mode": bpy.context.mode, "flagged": _flagged(buf.getvalue())})
    return steps


def bind(name):
    rig, body = bpy.data.objects["rig"], bpy.data.objects[name]
    buf = io.StringIO()
    steps = {}
    t0 = time.time()
    with contextlib.redirect_stdout(buf), _view3d():
        _object_mode()
        _select(rig)
        steps["match"] = list(bpy.ops.arp.match_to_rig("EXEC_DEFAULT"))
        _object_mode()
        bpy.context.scene.arp_bind_engine = "PSEUDO_VOXELS"
        _select(body, rig, active=rig)
        steps["bind"] = list(bpy.ops.arp.bind_to_rig("EXEC_DEFAULT"))
        _object_mode()
        _select(body)
        bpy.ops.object.vertex_group_normalize_all(group_select_mode="BONE_DEFORM", lock_active=False)
    deform = sorted(b.name for b in rig.data.bones if b.use_deform)
    steps.update({"seconds": round(time.time() - t0, 1), "bones": len(rig.data.bones), "deform": len(deform),
                  "groups": len(body.vertex_groups),
                  "modifiers": [(m.type, m.object.name if getattr(m, "object", None) else None) for m in body.modifiers],
                  "flagged": _flagged(buf.getvalue())})
    return steps


def _smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def weights(name, marks):
    """175's corrections. Both engines put upper-arm weight on the torso below the armpit, and 0.6 of the arm's
    weight on the chest and back just inside the shoulder joint; arms raised, the flanks ballooned. The arm's
    weight fades out of both, into the nearest spine bone (or the collarbone, high on the chest), then the
    shoulder region is smoothed, pruned under 0.01, held to 8 influences and normalised. The Humanoid man's
    distances, in metres at 1.80 m, scale by k; the armpit, torso edge and shoulder joint are this body's."""
    body, rig = bpy.data.objects[name], bpy.data.objects["rig"]
    me, vg = body.data, body.vertex_groups
    H = marks["height"]
    k = H / HUMAN_HEIGHT
    A, T = marks["weights"]["armpit"], marks["weights"]["torso_edge"]
    S = rig.data.bones["c_arm_twist_offset.l"].head_local
    SX, SZ = abs(S.x), S.z
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    W = np.zeros((n, len(vg)), np.float32)
    for v in me.vertices:
        for g in v.groups:
            W[v.index, g.group] = g.weight
    gi = {g.name: g.index for g in vg}
    spines = [s for s in ("spine_01.x", "spine_02.x", "spine_03.x", "spine_04.x", "spine_05.x") if s in gi]
    segs = [(np.array(rig.data.bones[s].head_local), np.array(rig.data.bones[s].tail_local)) for s in spines]

    def seg_dist(p, a, b):
        ab = b - a
        t = np.clip(((p - a) @ ab) / (ab @ ab), 0, 1)
        return np.linalg.norm(p - (a + np.outer(t, ab)), axis=1)

    nearest = np.array([gi[s] for s in spines])[np.argmin(np.stack([seg_dist(co, a, b) for a, b in segs], 1), 1)]
    moved = {"flank": 0.0, "medial": 0.0}
    for side, sgn in (("l", 1), ("r", -1)):
        arm = [gi["c_arm_twist_offset." + side], gi["arm_stretch." + side]]
        sh = gi["shoulder." + side]
        # 1. the flank below the armpit
        fl = (sgn * co[:, 0] > 0) & (np.abs(co[:, 0]) < T) & (co[:, 2] > A - 0.165 * k) & (co[:, 2] < A + 0.06 * k)
        f_arm = _smooth(A - 0.03 * k, A + 0.05 * k, co[:, 2])
        f_sh = _smooth(A - 0.08 * k, A + 0.02 * k, co[:, 2])
        rem = np.zeros(n, np.float32)
        for a in arm:
            r = np.where(fl, W[:, a] * (1 - f_arm), 0)
            W[:, a] -= r
            rem += r
        r = np.where(fl, W[:, sh] * (1 - f_sh), 0)
        W[:, sh] -= r
        rem += r
        np.add.at(W, (np.arange(n), nearest), rem)
        moved["flank"] += float(rem.sum())
        # 2. inward of the shoulder joint
        md = (sgn * co[:, 0] > 0) & (np.abs(co[:, 0]) < SX) & (co[:, 2] > A - 0.05 * k) & (co[:, 2] < SZ + 0.10 * k)
        f = 1 - _smooth(0.02 * k, 0.09 * k, SX - np.abs(co[:, 0]))
        rem = np.zeros(n, np.float32)
        for a in arm:
            r = np.where(md, W[:, a] * (1 - f), 0)
            W[:, a] -= r
            rem += r
        top = co[:, 2] > SZ - 0.05 * k
        W[:, sh] += np.where(top, rem, 0)
        np.add.at(W, (np.arange(n), nearest), np.where(~top, rem, 0))
        moved["medial"] += float(rem.sum())
    # 3. smooth the shoulder region, prune, limit to 8, normalise
    region = (np.abs(co[:, 0]) < T + 0.12 * k) & (co[:, 2] > A - 0.145 * k) & (co[:, 2] < SZ + 0.12 * k)
    ed = np.empty(len(me.edges) * 2, np.int32)
    me.edges.foreach_get("vertices", ed)
    ed = ed.reshape(-1, 2)
    deg = np.bincount(ed.ravel(), minlength=n).astype(np.float32)
    for _ in range(4):
        acc = np.zeros_like(W)
        np.add.at(acc, ed[:, 0], W[ed[:, 1]])
        np.add.at(acc, ed[:, 1], W[ed[:, 0]])
        W[region] = 0.5 * W[region] + 0.5 * (acc / np.maximum(deg, 1)[:, None])[region]
    W[W < 0.01] = 0.0
    order = np.argsort(-W, axis=1)
    keep = np.zeros_like(W, bool)
    np.put_along_axis(keep, order[:, :8], True, axis=1)
    W = np.where(keep, W, 0.0)
    W /= np.maximum(W.sum(1, keepdims=True), 1e-8)
    touched = np.nonzero(region | ((np.abs(co[:, 0]) < T) & (co[:, 2] > A - 0.165 * k) & (co[:, 2] < SZ + 0.10 * k)))[0]
    for g in vg:
        col = W[touched, g.index]
        for i in touched[col > 0]:
            g.add([int(i)], float(W[i, g.index]), "REPLACE")
        gone = touched[col == 0]
        if len(gone):
            g.remove([int(i) for i in gone])
    me.update()
    return {"k": round(k, 3), "armpit": A, "torso_edge": T, "shoulder_joint": [round(SX, 3), round(SZ, 3)],
            "moved": {key: round(v, 1) for key, v in moved.items()}, "touched": int(len(touched))}


def check(name):
    body = bpy.data.objects[name]
    me = body.data
    deform = {b.name for b in bpy.data.objects["rig"].data.bones if b.use_deform}
    names = {g.index: g.name for g in body.vertex_groups}
    cnt = np.zeros(len(me.vertices), int)
    tot = np.zeros(len(me.vertices))
    stray = set()
    for v in me.vertices:
        ws = [(names[g.group], g.weight) for g in v.groups if g.weight > 1e-4]
        cnt[v.index] = len(ws)
        tot[v.index] = sum(w for _, w in ws)
        stray.update(n for n, _ in ws if n not in deform)
    return {"vertices": len(me.vertices), "unweighted": int((cnt == 0).sum()), "max_influences": int(cnt.max()),
            "sum_min": round(float(tot.min()), 4), "sum_max": round(float(tot.max()), 4),
            "groups_not_deforming": sorted(stray)[:10]}


def jump_start(marks):
    """176's jump start: straight into the take-off, from take-off to the apex (frames 0-13, held to 15), arms FK
    and swinging forward to 85 degrees at most, feet IK and tucking. Angles are the Humanoid man's; his hip lift
    and foot lifts, in metres, scale by this body's root height over his."""
    rig = bpy.data.objects["rig"]
    pb = rig.pose.bones
    scn = bpy.context.scene
    kl = marks["markers"]["root"][2] / HUMAN_ROOT
    old = bpy.data.actions.get("JumpStart")
    if old is not None:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new("JumpStart")
    act.use_fake_user = True
    if rig.animation_data is None:
        rig.animation_data_create()
    rig.animation_data.action = act
    ctrl = ["c_root_master.x", "c_spine_01.x", "c_spine_02.x", "c_spine_03.x", "c_spine_04.x", "c_spine_05.x", "c_neck.x",
            "c_head.x", "c_shoulder.l", "c_shoulder.r", "c_arm_fk.l", "c_arm_fk.r", "c_forearm_fk.l", "c_forearm_fk.r",
            "c_hand_fk.l", "c_hand_fk.r", "c_foot_ik.l", "c_foot_ik.r"]
    ball = {s: Vector(rig.data.bones["toes_01." + s].head_local) for s in ("l", "r")}
    X, Y = Vector((1, 0, 0)), Vector((0, 1, 0))

    def reset():
        for b in pb:
            b.location = (0, 0, 0)
            b.rotation_quaternion = (1, 0, 0, 0)
            b.rotation_euler = (0, 0, 0)
            b.scale = (1, 1, 1)
        for c in ("c_hand_ik.l", "c_hand_ik.r"):
            pb[c]["ik_fk_switch"] = 1.0
        for c in ("c_foot_ik.l", "c_foot_ik.r"):
            pb[c]["ik_fk_switch"] = 0.0
        bpy.context.view_layer.update()

    def turn(bone, axis, deg, pivot=None):
        b = pb[bone]
        h = Vector(pivot) if pivot is not None else b.head.copy()
        b.matrix = Matrix.Translation(h) @ Matrix.Rotation(math.radians(deg), 4, axis) @ Matrix.Translation(-h) @ b.matrix
        bpy.context.view_layer.update()

    def move(bone, off):
        pb[bone].matrix = Matrix.Translation(Vector(off)) @ pb[bone].matrix
        bpy.context.view_layer.update()

    def pose(frame, lift, spine1, chest, head, feet, arms):
        reset()
        move("c_root_master.x", (0, 0, lift * kl))
        turn("c_spine_01.x", X, spine1)
        turn("c_spine_04.x", X, chest)
        turn("c_head.x", X, head)
        for side, sgn in (("l", 1), ("r", -1)):
            off, toe = feet[side]
            off = tuple(v * kl for v in off)
            move("c_foot_ik." + side, off)
            turn("c_foot_ik." + side, X, toe, pivot=ball[side] + Vector(off))
            swing, abduct, elbow = arms
            turn("c_arm_fk." + side, X, swing)
            turn("c_arm_fk." + side, Y, -sgn * abduct)
            turn("c_forearm_fk." + side, X, elbow)
        for c in ctrl:
            pb[c].keyframe_insert("location", frame=frame)
            pb[c].keyframe_insert("rotation_euler", frame=frame)
        for c in ("c_hand_ik.l", "c_hand_ik.r", "c_foot_ik.l", "c_foot_ik.r"):
            pb[c].keyframe_insert('["ik_fk_switch"]', frame=frame)

    pose(0, 0.03, 8, -6, -4, {"l": ((0, 0.00, 0.00), 35), "r": ((0, 0.00, 0.00), 35)}, (-50, 0, -15))
    pose(3, 0.04, 3, -8, -6, {"l": ((0, 0.02, 0.03), 45), "r": ((0, 0.02, 0.03), 45)}, (-85, 5, -12))
    pose(7, 0.02, 6, -2, 0, {"l": ((0, 0.07, 0.15), 25), "r": ((0, 0.06, 0.13), 28)}, (-70, 15, -25))
    pose(13, 0.00, 10, 4, 2, {"l": ((0, 0.11, 0.28), 15), "r": ((0, 0.08, 0.23), 18)}, (-40, 25, -35))
    pose(15, 0.00, 10, 4, 2, {"l": ((0, 0.11, 0.28), 15), "r": ((0, 0.08, 0.23), 18)}, (-40, 25, -35))
    scn.frame_start, scn.frame_end = 0, 15
    # The keys are frames at 30 a second (0.50 s to the apex's hold). A new file is at Blender's 24, which
    # stretched the Martian's to 0.67 s in his first export (182).
    scn.render.fps, scn.render.fps_base = 30, 1.0
    scn.frame_set(0)
    return {"action": act.name, "fps": scn.render.fps, "range": list(act.frame_range), "kl": round(kl, 3), "fcurves": len(act.fcurves) if hasattr(act, "fcurves") else None}


def _pose_tools(rig):
    pb = rig.pose.bones
    X, Y = Vector((1, 0, 0)), Vector((0, 1, 0))

    def reset():
        for b in pb:
            b.location = (0, 0, 0)
            b.rotation_quaternion = (1, 0, 0, 0)
            b.rotation_euler = (0, 0, 0)
            b.scale = (1, 1, 1)
        for c in ("c_hand_ik.l", "c_hand_ik.r"):
            pb[c]["ik_fk_switch"] = 1.0
        for c in ("c_foot_ik.l", "c_foot_ik.r"):
            pb[c]["ik_fk_switch"] = 0.0
        bpy.context.view_layer.update()

    def turn(bone, axis, deg, pivot=None):
        b = pb[bone]
        h = Vector(pivot) if pivot is not None else b.head.copy()
        b.matrix = Matrix.Translation(h) @ Matrix.Rotation(math.radians(deg), 4, axis) @ Matrix.Translation(-h) @ b.matrix
        bpy.context.view_layer.update()

    def move(bone, off):
        pb[bone].matrix = Matrix.Translation(Vector(off)) @ pb[bone].matrix
        bpy.context.view_layer.update()

    return pb, X, Y, reset, turn, move


def pose_tests(name, marks, out_dir):
    """175's poses -- T-pose, deep squat, full stride, arms overhead -- rendered front and three-quarter beside the
    rest pose, with how far the flank below the armpit moves in each (the Humanoid man's held to a mean of 0.2 cm,
    and his arms-overhead band bulged by linear blend skinning, 175's known limit)."""
    rig, body = bpy.data.objects["rig"], bpy.data.objects[name]
    pb, X, Y, reset, turn, move = _pose_tools(rig)
    H = marks["height"]
    k, kl = H / HUMAN_HEIGHT, marks["markers"]["root"][2] / HUMAN_ROOT
    A, T = marks["weights"]["armpit"], marks["weights"]["torso_edge"]
    rest = np.array([v.co[:] for v in body.data.vertices])
    flank = (np.abs(rest[:, 0]) < 0.8 * T) & (rest[:, 2] > A - 0.165 * k) & (rest[:, 2] < A - 0.03 * k)
    ball = {s: Vector(rig.data.bones["toes_01." + s].head_local) for s in ("l", "r")}
    saved_action = rig.animation_data.action if rig.animation_data else None
    if rig.animation_data:
        rig.animation_data.action = None

    def arm_down_angle(side):
        b = rig.data.bones["arm_stretch." + side] if "arm_stretch." + side in rig.data.bones else rig.data.bones["c_arm_fk." + side]
        d = b.tail_local - b.head_local
        return math.degrees(math.atan2(-d.z, abs(d.x)))

    def t_pose():
        for side, sgn in (("l", 1), ("r", -1)):
            turn("c_arm_fk." + side, Y, -sgn * arm_down_angle(side))

    def squat():
        move("c_root_master.x", (0, 0, -0.42 * kl))
        turn("c_spine_01.x", X, 20)

    def stride():
        for side, fwd, toe in (("l", -0.40, 0), ("r", 0.35, 30)):
            off = (0, fwd * kl, 0.04 * kl if toe else 0)
            move("c_foot_ik." + side, off)
            if toe:
                turn("c_foot_ik." + side, X, toe, pivot=ball[side] + Vector(off))

    def overhead():
        for side, sgn in (("l", 1), ("r", -1)):
            turn("c_shoulder." + side, Y, -sgn * 30)
            turn("c_arm_fk." + side, Y, -sgn * (arm_down_angle(side) + 110 - 30))

    poses = [("rest", lambda: None), ("tpose", t_pose), ("squat", squat), ("stride", stride), ("overhead", overhead)]
    sc = bpy.context.scene
    prev = (sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.camera, sc.render.filepath)
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 500, 700
    cd = bpy.data.cameras.new("PoseCam")
    cam = bpy.data.objects.new("PoseCam", cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    os.makedirs(out_dir, exist_ok=True)
    tiles, moved = {}, {}
    for pname, fn in poses:
        reset()
        fn()
        dg = bpy.context.evaluated_depsgraph_get()
        ev = body.evaluated_get(dg).to_mesh()
        now = np.array([v.co[:] for v in ev.vertices])
        body.evaluated_get(dg).to_mesh_clear()
        d = np.linalg.norm(now - rest, axis=1)[flank] * 100
        moved[pname] = {"flank_mean_cm": round(float(d.mean()), 2), "flank_worst_cm": round(float(d.max()), 2)}
        for view, loc in (("front", (0, -3.2 * k, 0.5 * H)), ("34", (2.2 * k, -2.4 * k, 0.6 * H))):
            cd.type, cd.lens = "PERSP", 50
            cam.location = loc
            cam.rotation_euler = (Vector((0, 0, 0.5 * H)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
            path = "%s/%s_pose_%s_%s.png" % (out_dir, name, pname, view)
            sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
            tiles[(pname, view)] = path
    reset()
    if saved_action is not None:
        rig.animation_data.action = saved_action
    bpy.data.objects.remove(cam)
    bpy.data.cameras.remove(cd)
    sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.camera, sc.render.filepath = prev
    rows = []
    for view in ("34", "front"):                       # bottom row first: Blender's pixels run bottom-up
        row = []
        for pname, _ in poses:
            im = bpy.data.images.load(tiles[(pname, view)])
            a = np.empty(im.size[0] * im.size[1] * 4, np.float32)
            im.pixels.foreach_get(a)
            row.append(a.reshape(im.size[1], im.size[0], 4))
            bpy.data.images.remove(im)
        rows.append(np.concatenate(row, 1))
    g = np.concatenate(rows, 0)
    sheet = bpy.data.images.new("pose_sheet", g.shape[1], g.shape[0])
    sheet.pixels.foreach_set(g.ravel())
    sheet.filepath_raw = "%s/%s_pose_tests.png" % (out_dir, name)
    sheet.file_format = "PNG"
    sheet.save()
    bpy.data.images.remove(sheet)
    return {"sheet": "%s/%s_pose_tests.png" % (out_dir, name), "flank_vertices": int(flank.sum()), "moved": moved}


def frames_sheet(name, marks, out_dir, frames=(0, 3, 7, 13), tag="jumpstart", views=("side", "34")):
    """The current action at the given frames, from each view, in one sheet: the key poses Joe sees."""
    rig = bpy.data.objects["rig"]
    H = marks["height"]
    k = H / HUMAN_HEIGHT
    places = {"side": (3.4 * k, 0, 0.55 * H), "34": (2.2 * k, -2.4 * k, 0.6 * H), "front": (0, -3.4 * k, 0.55 * H)}
    sc = bpy.context.scene
    prev = (sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.camera, sc.render.filepath, sc.frame_current)
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 500, 700
    cd = bpy.data.cameras.new("SheetCam")
    cam = bpy.data.objects.new("SheetCam", cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    for view in views:
        loc = places[view]
        row = []
        for f in frames:
            sc.frame_set(f)
            cam.location = loc
            cam.rotation_euler = (Vector((0, 0, 0.55 * H)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
            path = "%s/%s_%s_f%02d_%s.png" % (out_dir, name, tag, f, view)
            sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
            im = bpy.data.images.load(path)
            a = np.empty(im.size[0] * im.size[1] * 4, np.float32)
            im.pixels.foreach_get(a)
            row.append(a.reshape(im.size[1], im.size[0], 4))
            bpy.data.images.remove(im)
        rows.append(np.concatenate(row, 1))
    bpy.data.objects.remove(cam)
    bpy.data.cameras.remove(cd)
    sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.camera, sc.render.filepath, f0 = prev
    sc.frame_set(f0)
    g = np.concatenate(rows, 0)
    sheet = bpy.data.images.new("frames_sheet", g.shape[1], g.shape[0])
    sheet.pixels.foreach_set(g.ravel())
    out = "%s/%s_%s_keyposes.png" % (out_dir, name, tag)
    sheet.filepath_raw = out
    sheet.file_format = "PNG"
    sheet.save()
    bpy.data.images.remove(sheet)
    return {"sheet": out}


EXPORT = dict(arp_engine_type="UNREAL", arp_export_rig_type="HUMANOID", arp_ue4=False, arp_rename_for_ue=True,
              arp_mannequin_axes=True, arp_ue_ik=False, arp_export_twist=True, arp_keep_bend_bones=False,
              arp_full_facial=False, arp_ue_root_motion=False, arp_bake_anim=True, arp_bake_only_active=True,
              arp_units_x100=True, arp_global_scale=1.0, arp_ge_sel_only=False, arp_export_tex=False,
              arp_export_separate_fbx=False, arp_ge_export_metacarp=True, arp_ge_force_rest_pose_export=True)


def export(name, action="JumpStart", fbx_name=None, maps=True, fps=30):
    """The FBX and the four maps into client/RawContent/Characters/<name>/, with 175's settings.

    Auto-Rig Pro bakes only the active action (arp_bake_only_active), over the scene's frame range, so both are
    set from the action here rather than left to whatever was last played: <name>.fbx is the body and its jump
    start, <name>_Sprint.fbx the body and its sprint (191)."""
    scn = bpy.context.scene
    rig, body = bpy.data.objects["rig"], bpy.data.objects[name]
    act = bpy.data.actions[action]
    rig.animation_data.action = act
    scn.frame_start, scn.frame_end = int(act.frame_range[0]), int(act.frame_range[1])
    scn.render.fps, scn.render.fps_base = fps, 1.0
    out_dir = RAW + name + "/"
    os.makedirs(out_dir, exist_ok=True)
    applied = {}
    for key, v in EXPORT.items():
        if not hasattr(scn, key):
            applied[key] = "MISSING"
            continue
        setattr(scn, key, v)
        applied[key] = getattr(scn, key)
    fbx = out_dir + (fbx_name or name) + ".fbx"
    buf = io.StringIO()
    t0 = time.time()
    with contextlib.redirect_stdout(buf), _view3d():
        _object_mode()
        _select(body, rig, active=rig)
        ret = list(bpy.ops.arp.arp_export_fbx_panel("EXEC_DEFAULT", filepath=fbx))
    written = {}
    for role in ("BaseColor", "Normal", "Roughness", "Metallic") if maps else ():
        img = bpy.data.images["T_%s_%s" % (name, role)]
        path = out_dir + "T_%s_%s.png" % (name, role)
        img.filepath_raw = path
        img.file_format = "PNG"
        img.save()
        written[role] = [img.colorspace_settings.name, list(img.size), round(os.path.getsize(path) / 1e6, 2)]
    return {"ret": ret, "seconds": round(time.time() - t0, 1), "fbx": fbx, "action": action,
            "frames": [scn.frame_start, scn.frame_end],
            "fbx_kb": round(os.path.getsize(fbx) / 1024) if os.path.exists(fbx) else None,
            "missing_settings": [key for key, v in applied.items() if v == "MISSING"], "maps": written,
            "flagged": _flagged(buf.getvalue())}


# --- The sprint (191) -------------------------------------------------------------------------------------------

SPRINT_SPEED = 10.8   # m/s: WalkSpeed 6 x SprintMultiplier 1.8 (136), which Joe kept on 11 October (191)

# Keys a second in the sprint clips. Not 30, the other clips' rate: Unreal interpolates each joint's rotation between
# keys, and a planted ball moving 36 cm between keys a thirtieth of a second apart swung through an arc that sank it up
# to 4.4 cm into the ground halfway between them on the Humanoid man (SpaceMMO.Character.EachRaceSprintsOnItsOwnFeet,
# 11 October). Blender solves the foot on a straight line at every frame, so nothing measured there could show it.
# At 60 the chord halves and the sag falls by about four. GAITS stays in thirtieths, the poses Joe approved.
SPRINT_FPS = 60

# One sprint per body (191). Lengths are in metres at the Humanoid man's proportions and scale by kl, this body's
# root height over his; angles are degrees; frames are thirtieths of a second, keyed at SPRINT_FPS.
#   cycle    frames from a foot's touchdown to its next. The clip holds two, because the library's jog it blends
#            with holds two in its 32 frames, left foot down at about 0 and 16, right at 8 and 24 (measured on the
#            Humanoid man, 191). ABP_Human's blend space keeps them in step by the footfall markers the clip
#            importer reads off both, in its sync group (191, 13 October); before that it kept them in no step at all.
#   contact  frames a foot is on the ground. It sweeps SPRINT_SPEED x contact / 30 back under the body then, so
#            a leg's reach bounds it: 0.72 m on two frames, 1.08 on three.
#   ahead    where the ball of the foot lands in front of the hip; the rest of the sweep is behind it.
#   drive, fold   the swing, as a body moves it: how far forward the thigh drives (degrees from hanging) and how
#            far the knee folds as the heel comes through. The swing is authored in joint angles, not as a path
#            for the foot: a path for the foot left the knee to fold to 151 degrees and the ankle to bend 70
#            degrees past neutral toward the shin, and Joe, 11 October, saw "the feet and legs ... all warped".
# Where the joints sit across the body is not here: it is the run's (RUN_SHAPE, below).
GAITS = {
    # The baseline: a strong forward lean, high heel recovery, arms driving hard past the hips. Landing 0.30 ahead
    # left his push-off on a locked knee (0.3 degrees), the ankle swinging on the whole leg's length around the hip,
    # and between keys his ball sank 1.90 cm into the ground even at 60 a second; 0.38 pushes off on a 37-degree knee
    # and it sinks 0.74 (verify_export.py's sag check, on trial exports of 0.34, 0.36 and 0.38).
    "HumanoidMale": dict(cycle=14, contact=2, ahead=0.38, drop=0.065, bob=0.025, lean=12, chest=3, head=-13,
                         pelvis_yaw=7, chest_yaw=9, arm_mid=5, arm_amp=45, adduct=22, elbow_mid=85, elbow_amp=10,
                         drive=62, fold=120),
    # A long, light stride with little bob (176): a slower cycle on longer legs, contact on three frames. Landing
    # 0.30 ahead left 0.68 m of the 1.08 m sweep behind him, past his reach at toe-off (1.03 of his leg, and 8 of
    # 12 planted steps landed); 0.40 ahead centres it.
    "MartianMale": dict(cycle=18, contact=3, ahead=0.40, drop=0.09, bob=0.012, lean=9, chest=2, head=-10,
                        pelvis_yaw=6, chest_yaw=7, arm_mid=4, arm_amp=40, adduct=20, elbow_mid=85, elbow_amp=10,
                        drive=58, fold=110),
    # Upright and light-footed (176): little lean, the heel kicked high, a quick turnover.
    "SpaceElfMale": dict(cycle=16, contact=2, ahead=0.28, drop=0.06, bob=0.018, lean=6, chest=1, head=-6,
                         pelvis_yaw=6, chest_yaw=8, arm_mid=5, arm_amp=42, adduct=22, elbow_mid=85, elbow_amp=10,
                         drive=66, fold=122),
    # Heavy and wide (176): more lean and drop, feet apart, arms swinging round his chest rather than past it. His
    # arms hang 40 degrees out at rest; left there, his fists swung 0.8-0.9 m from his midline against a torso
    # 0.37 m wide at the armpit, and read as flailing. 18 in keeps them just outside his chest.
    # At 0.08 his deeper knee bent the ankle 22.9 degrees up at mid-stance, past LIMITS.
    "SpaceOrcMale": dict(cycle=16, contact=2, ahead=0.30, drop=0.065, bob=0.03, lean=14, chest=4, head=-15,
                         pelvis_yaw=8, chest_yaw=11, arm_mid=4, arm_amp=40, adduct=18, elbow_mid=90, elbow_amp=10,
                         drive=55, fold=108),
}

# What a leg may do, checked on every frame by sprint_measure. The ankle is the angle between shin and foot
# against its rest angle: a real one dorsiflexes about 20 degrees and plantarflexes about 50. A knee folded past
# about 130 degrees collapses its back under linear blend skinning. A boot bent more than 30 at the ball creases.
# shape_max is how far, in centimetres, an elbow, hand or knee may sit across the body from where the run has it at
# the same point of the stride; an ankle gets more, because a planted one holds still where the run's drifts.
LIMITS = dict(knee_max=130.0, dorsiflex_max=22.0, plantarflex_max=50.0, toe_bend_max=32.0, shape_max=1.0,
              ankle_shape_max=2.5)

# Where the run every race plays puts each joint across the body, at each fraction of its two strides, measured on
# each body in Unreal by ue_measure_run.py (191). Joe, 13 October, after playing the first sprints: going from the
# run to the sprint "the arms and legs move much differently outward/inward". The run swings each hand across the
# chest at the front of its stride and out past the hip at the back and flares the knees through the swing; those
# sprints swung the arms straight and held the knees in line, and every blend between them dragged the joints
# sideways, up to 29 cm on the orc. The blend space keeps the two in step by footfall (the clip importer's markers,
# in ABP_Human's sync group), and both land each foot at nearly the same fractions of their length, so matching the
# run at each fraction leaves a blend between them nothing to move sideways. Fore-aft and up-down stay the sprint's
# own.
RUN_SHAPE = "D:/Programming/SpaceMMO/tools/characters/run_shape.json"


def _run_shape(name):
    """The run's joints on this body: at(joint, t) is where the run puts joint ('hand_l', 'calf_r' ...) at fraction
    t of its clip, in metres in the rig's axes. Unreal's are the same but for forward, which is +y there."""
    with open(RUN_SHAPE) as f:
        data = json.load(f)["bodies"][name]
    tracks = {j: np.array(v, float) for j, v in data["tracks"].items()}
    keys = data["keys"]

    def at(joint, t):
        k = (t % 1.0) * (keys - 1)
        i = min(int(math.floor(k)), keys - 2)
        a, b = tracks[joint][i], tracks[joint][i + 1]
        x, y, z = (a + (b - a) * (k - i)) / 100.0
        return Vector((x, -y, z))
    return at


def _solve_across(a, b, c):
    """The angle nearest nought with a cos(angle) + b sin(angle) = c, or the nearest it can get."""
    r = math.hypot(a, b)
    if r < 1e-9:
        return 0.0
    base = math.atan2(b, a)
    spread = math.acos(max(-1.0, min(1.0, c / r)))
    cands = [math.remainder(base + spread, 2 * math.pi), math.remainder(base - spread, 2 * math.pi)]
    return min(cands, key=abs)


def _turn_across(v, axis, target_x):
    """How far to turn vector v about unit axis, in radians, for its x to be target_x (Rodrigues' formula, the
    same right-hand turn Matrix.Rotation makes)."""
    along = axis * axis.dot(v)
    perp = v - along
    return _solve_across(perp.x, axis.cross(perp).x, target_x - along.x)
TOE_OFF_PITCH = 38.0   # the heel's lift about the ball at toe-off; 55 bent the boot 47 degrees at the toes


def _hermite(points, s):
    """Cubic Hermite through (s, value, tangent-or-None) rows; a None tangent is Catmull-Rom's."""
    ss = [p[0] for p in points]
    i = max(0, min(len(points) - 2, int(np.searchsorted(ss, s, side="right")) - 1))
    s0, v0, m0 = points[i]
    s1, v1, m1 = points[i + 1]
    h = s1 - s0

    def tangent(j):
        if points[j][2] is not None:
            return np.asarray(points[j][2], float)
        a, b = points[max(j - 1, 0)], points[min(j + 1, len(points) - 1)]
        return (np.asarray(b[1], float) - np.asarray(a[1], float)) / (b[0] - a[0])

    t = (s - s0) / h
    t2, t3 = t * t, t * t * t
    return ((2 * t3 - 3 * t2 + 1) * np.asarray(v0, float) + (t3 - 2 * t2 + t) * h * tangent(i)
            + (-2 * t3 + 3 * t2) * np.asarray(v1, float) + (t3 - t2) * h * tangent(i + 1))


def _down_angle(v):
    """A vector's angle in the side view, in degrees from straight down, positive toward the front (-Y)."""
    return math.degrees(math.atan2(-v.y, -v.z))


def _leg_angles(pb, side):
    """The posed leg in the side view: thigh from hanging, knee fold, and the shin-to-foot angle."""
    hip, knee, ankle, ball = (pb["thigh_ik." + side].head, pb["leg_ik." + side].head, pb["foot." + side].head,
                              pb["toes_01." + side].head)
    thigh, shin = _down_angle(knee - hip), _down_angle(ankle - knee)
    return np.array([thigh, thigh - shin, _down_angle(ball - ankle) - shin])


def sprint(name):
    """The Sprint action on this body's rig, from GAITS[name]: two strides, every frame keyed, the last equal to
    the first so it loops, in place with the root still. Returns what was measured off the posed rig.

    A planted foot is placed by its ball, which slides back at exactly SPRINT_SPEED (the number 176 asks for). A
    swinging leg is placed by its joints: the thigh's angle, the knee's fold and the ankle's angle against the shin,
    splined from the planted pose it leaves to the one it lands in, through a heel recovery and a knee drive."""
    g = GAITS[name]
    shape = _run_shape(name)
    rig = bpy.data.objects["rig"]
    pb, X, Y, reset, turn, move = _pose_tools(rig)
    scn = bpy.context.scene
    kl = rig.data.bones["c_root_master.x"].head_local.z / HUMAN_ROOT
    step = SPRINT_FPS // 30
    n, c = g["cycle"] * step, g["contact"] * step
    frames = 2 * n
    per_frame = SPRINT_SPEED / SPRINT_FPS
    old = bpy.data.actions.get("Sprint")
    if old is not None:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new("Sprint")
    act.use_fake_user = True
    if rig.animation_data is None:
        rig.animation_data_create()
    rig.animation_data.action = act
    Z = Vector((0, 0, 1))
    ball = {s: Vector(rig.data.bones["toes_01." + s].head_local) for s in ("l", "r")}
    ankle_rest = {s: Vector(rig.data.bones["foot." + s].head_local) for s in ("l", "r")}
    pole_rest = {s: Vector(rig.data.bones["c_leg_pole." + s].head_local) for s in ("l", "r")}
    thigh_len = {s: rig.data.bones["thigh_ik." + s].length for s in ("l", "r")}
    shin_len = {s: rig.data.bones["leg_ik." + s].length for s in ("l", "r")}
    foot_rest = {s: _down_angle(ball[s] - ankle_rest[s]) for s in ("l", "r")}
    ctrl = ["c_root_master.x", "c_spine_01.x", "c_spine_02.x", "c_spine_03.x", "c_spine_04.x", "c_spine_05.x",
            "c_neck.x", "c_head.x", "c_shoulder.l", "c_shoulder.r", "c_arm_fk.l", "c_arm_fk.r", "c_forearm_fk.l",
            "c_forearm_fk.r", "c_hand_fk.l", "c_hand_fk.r", "c_foot_ik.l", "c_foot_ik.r", "c_toes_ik.l", "c_toes_ik.r",
            "c_leg_pole.l", "c_leg_pole.r"]
    fingers = [f + j for f in ("c_index", "c_middle", "c_ring", "c_pinky") for j in ("1", "2", "3")]
    ctrl = ctrl + [f + "." + s for f in fingers for s in ("l", "r")]
    arm_turns = {"across": 0.0, "roll": 0.0}

    def wave(p, peak):
        return math.cos(2 * math.pi * (p - peak))

    def bend_elbow(side, flex, lateral):
        """The forearm, `flex` degrees forward of the upper arm's line, in the plane the arm swings in. Bending it in
        the plane of its rest bend instead took the A-pose's outward hang with it: the first render had the front
        hand up beside the head, palm out, like a wave."""
        up = pb["c_arm_fk." + side]
        fo = pb["c_forearm_fk." + side]
        u, f = (up.tail - up.head).normalized(), (fo.tail - fo.head).normalized()
        want = Matrix.Rotation(math.radians(-flex), 3, lateral) @ u
        q = f.rotation_difference(want)
        if q.angle > 1e-6:
            turn("c_forearm_fk." + side, q.axis, math.degrees(q.angle))

    def pose_body(p, t):
        """Everything but the legs at phase p of a stride, which is fraction t of the clip."""
        reset()
        # The hips: lowered, bobbing twice a stride (lowest mid-stance, highest mid-flight), turning toward the
        # leg that swings forward. The left leg is furthest forward just before its touchdown, at p ~ 0.92.
        flight_mid = (c / n + 0.5) / 2
        move("c_root_master.x", (0, 0, (-g["drop"] + g["bob"] * math.cos(4 * math.pi * (p - flight_mid))) * kl))
        pelvis = -g["pelvis_yaw"] * wave(p, 0.92)
        chest = -g["chest_yaw"] * wave(p, 0.42)
        turn("c_root_master.x", Z, pelvis)
        turn("c_spine_01.x", X, g["lean"])
        turn("c_spine_02.x", Z, 0.5 * (chest - pelvis))
        turn("c_spine_03.x", Z, 0.5 * (chest - pelvis))
        turn("c_spine_04.x", X, g["chest"])
        turn("c_head.x", X, g["head"])
        turn("c_head.x", Z, -chest)
        # The arms: each opposite its own leg, so the left drives forward as the right leg does (p ~ 0.42).
        # A bone's local Y runs along it, so the chest's is up its spine; its X is across it, and forward is the
        # cross of the two. Taking local -Y for forward twisted the arms backward instead of bringing them in.
        chest_m = pb["c_spine_05.x"].matrix.to_3x3()
        lateral = (chest_m @ Vector((1, 0, 0))).normalized()
        forward = lateral.cross((chest_m @ Vector((0, 1, 0))).normalized()).normalized()
        for side, sgn, peak in (("l", 1, 0.42), ("r", -1, 0.92)):
            w = wave(p, peak)
            turn("c_arm_fk." + side, forward, -sgn * g["adduct"])   # in toward the body; negative swings out
            turn("c_arm_fk." + side, lateral, -(g["arm_mid"] + g["arm_amp"] * w))
            bend_elbow(side, g["elbow_mid"] + g["elbow_amp"] * w, lateral)
            # A loose hand: negative X curls these toward the palm on both sides.
            for f in fingers:
                pb[f + "." + side].rotation_euler = (math.radians(-35), 0, 0)
        bpy.context.view_layer.update()
        # Then across the body, where the run has them at this fraction of its clip: the elbow by turning the whole
        # arm about the chest's forward axis, then the hand by turning the upper arm about its own length, which
        # keeps the elbow a hinge, as a person carries a hand across the chest.
        for side in ("l", "r"):
            shoulder = pb["c_arm_fk." + side].head.copy()
            elbow = pb["c_forearm_fk." + side].head.copy()
            across = _turn_across(elbow - shoulder, forward, shape("lowerarm_" + side, t).x - shoulder.x)
            turn("c_arm_fk." + side, forward, math.degrees(across), pivot=shoulder)
            elbow = pb["c_forearm_fk." + side].head.copy()
            length = (elbow - shoulder).normalized()
            roll = _turn_across(pb["c_hand_fk." + side].head - shoulder, length, shape("hand_" + side, t).x - shoulder.x)
            turn("c_arm_fk." + side, length, math.degrees(roll), pivot=shoulder)
            arm_turns["across"] = max(arm_turns["across"], abs(math.degrees(across)))
            arm_turns["roll"] = max(arm_turns["roll"], abs(math.degrees(roll)))

    def aim_knee(side, t):
        """The knee's pole, turned about the line from hip to ankle until the knee sits across the body where the
        run's does at fraction t: the knee goes round a circle about that line, and the pole sets where on it. (The
        first sprints kept the pole in the leg's plane, the knee straight ahead, where the run flares it.)"""
        hip, ankle = pb["thigh_ik." + side].head.copy(), pb["c_foot_ik." + side].head.copy()
        a, b = thigh_len[side], shin_len[side]
        d = min((ankle - hip).length, 0.999 * (a + b))
        u = (ankle - hip).normalized()
        along = (a * a - b * b + d * d) / (2 * d)
        radius = math.sqrt(max(a * a - along * along, 1e-8))
        centre = hip + u * along
        ahead = Vector((0, -1, 0))
        ahead = (ahead - u * ahead.dot(u)).normalized()
        side_way = u.cross(ahead).normalized()
        angle = _solve_across(radius * ahead.x, radius * side_way.x, shape("calf_" + side, t).x - centre.x)
        want = centre + (ahead * math.cos(angle) + side_way * math.sin(angle)) * 0.6
        move("c_leg_pole." + side, want - pb["c_leg_pole." + side].head)

    def planted_x(side, t_down):
        """Where a planted ankle sits across the body: where the run's is as that foot lands, held there, since a
        planted foot cannot move sideways. The run's drifts by a centimetre and a half while down."""
        return shape("foot_" + side, t_down).x

    def pose_planted(side, sgn, f, t, t_down):
        """Frame f of the stance, fraction t of the clip, the foot down since t_down: the ball on the ground sliding
        back at SPRINT_SPEED, the heel rising about it, the toes bent flat as far as a boot bends and no further."""
        u = f / c
        pitch = 12 + (TOE_OFF_PITCH - 12) * u * u
        off = Vector((planted_x(side, t_down) - ankle_rest[side].x, -g["ahead"] * kl + per_frame * f - ball[side].y, 0))
        move("c_foot_ik." + side, off)
        turn("c_foot_ik." + side, X, pitch, pivot=ball[side] + off)
        turn("c_toes_ik." + side, X, -min(pitch, LIMITS["toe_bend_max"] - 2), pivot=ball[side] + off)
        aim_knee(side, t)

    def pose_swinging(side, sgn, s, spline, t, t_up, t_down):
        """Fraction s of the swing, fraction t of the clip, the foot up since t_up after landing at t_down: the ankle
        placed by the thigh and knee, the foot turned about it to its angle against the shin, the toes straightening
        early. Across the body it follows the run's ankle, easing out over the swing's first third the gap between
        where it was held planted and where the run's had drifted to."""
        thigh, fold, ankle_angle = spline(s)
        hip = pb["thigh_ik." + side].head.copy()
        held = planted_x(side, t_down) - shape("foot_" + side, t_up).x
        x = shape("foot_" + side, t).x + held * (1.0 - float(_smooth(0.0, 0.3, s)))
        # The leg's lengths in the side view: the hip and ankle are this far apart across the body.
        lat = abs(x - hip.x) / (thigh_len[side] + shin_len[side])
        lt, ls = thigh_len[side] * math.sqrt(1 - lat * lat), shin_len[side] * math.sqrt(1 - lat * lat)
        shin = thigh - fold
        r = math.radians
        target = Vector((x, hip.y - lt * math.sin(r(thigh)) - ls * math.sin(r(shin)),
                         hip.z - lt * math.cos(r(thigh)) - ls * math.cos(r(shin))))
        move("c_foot_ik." + side, target - ankle_rest[side])
        # Turn the foot about the ankle so its angle from hanging is the shin's plus the ankle's: positive X turns
        # the toes down, which lowers that angle.
        turn("c_foot_ik." + side, X, foot_rest[side] - (shin + ankle_angle), pivot=target)
        bend = max(0.0, 1.0 - s / 0.2) * min(TOE_OFF_PITCH, LIMITS["toe_bend_max"] - 2)
        if bend:
            turn("c_toes_ik." + side, X, -bend, pivot=pb["toes_01." + side].head.copy())
        aim_knee(side, t)

    # The knee's pole follows the foot in this rig (Auto-Rig Pro's pole_parent 1). A heel kicked up behind took
    # the pole with it, and the knee bent backward toward it; with 0 it stays with the root. Keyed every frame, and
    # put back by rest_on_jump(), so the jump start keeps its own.
    for side in ("l", "r"):
        pb["c_leg_pole." + side]["pole_parent"] = 0
    bpy.context.view_layer.update()
    phases = {"l": (1, 0.0), "r": (-1, 0.5)}

    def foot_frame(frame, shift):
        """For one foot at a frame of the clip: its frame within its stride, and the clip fractions at which it last
        landed and lifted."""
        f = ((frame / n + shift) % 1.0) * n
        down = (frame - round(f) if f <= c + 1e-9 else frame - f) % frames
        return f, down / frames, ((down + c) % frames) / frames

    # First pass: the planted frames alone, to read the leg's angles where the swing must leave and land.
    planted = {}
    for side, (sgn, shift) in phases.items():
        rows = []
        for f in range(c + 1):
            frame = round((f / n - shift) % 1.0 * n)
            pose_body((frame % n) / n, frame / frames)
            pose_planted(side, sgn, f, frame / frames, foot_frame(frame, shift)[1])
            rows.append(_leg_angles(pb, side))
        planted[side] = rows

    def swing_spline(side):
        """The swing's joint angles, from the planted pose the foot leaves to the one it lands in. The ankle's knots
        are offsets from this body's rest angle, so every body is held to the same range of motion. The tangents
        where it leaves and lands are a seventh of the planted frames': a planted leg sweeps some 48 degrees a
        frame at this speed, and carried into the swing at even half that, the thigh swung 117 degrees behind and
        the knee folded backward before turning."""
        rows = planted[side]
        swing = n - c
        ankle0 = _rest_ankle[side]
        knots = [
            (0.00, rows[c], 0.15 * (rows[c] - rows[c - 1]) * swing),
            (0.30, np.array([-6.0, g["fold"], ankle0 - 25]), None),             # heel through, foot trailing
            (0.56, np.array([g["drive"], g["fold"] - 22, ankle0 - 6]), None),   # knee driven high, shin back
            (0.80, np.array([g["drive"] - 14, 52.0, ankle0 - 2]), None),        # unfolding, shin reaching
            (1.00, rows[0], 0.15 * (rows[1] - rows[0]) * swing),
        ]

        def at(s):
            thigh, fold, ankle = _hermite(knots, s)
            return thigh, max(fold, 5.0), ankle
        return at

    reset()
    _rest_ankle = {s: float(_leg_angles(pb, s)[2]) for s in ("l", "r")}
    splines = {side: swing_spline(side) for side in phases}
    for frame in range(frames + 1):
        p = (frame % n) / n
        t = frame / frames
        pose_body(p, t)
        for side, (sgn, shift) in phases.items():
            f, t_down, t_up = foot_frame(frame, shift)
            if f <= c + 1e-9:
                pose_planted(side, sgn, round(f), t, t_down)
            else:
                pose_swinging(side, sgn, (f - c) / (n - c), splines[side], t, t_up, t_down)
        for ctl in ctrl:
            pb[ctl].keyframe_insert("location", frame=frame)
            pb[ctl].keyframe_insert("rotation_euler", frame=frame)
        for ctl in ("c_hand_ik.l", "c_hand_ik.r", "c_foot_ik.l", "c_foot_ik.r"):
            pb[ctl].keyframe_insert('["ik_fk_switch"]', frame=frame)
        for ctl in ("c_leg_pole.l", "c_leg_pole.r"):
            pb[ctl].keyframe_insert('["pole_parent"]', frame=frame)
    scn.frame_start, scn.frame_end = 0, frames
    scn.render.fps, scn.render.fps_base = SPRINT_FPS, 1.0
    scn.frame_set(0)
    out = sprint_measure(name)
    out.update({"action": act.name, "frames": frames + 1, "kl": round(kl, 3),
                "arm_turns_max_deg": {k: round(v, 1) for k, v in arm_turns.items()},
                "planted_angles": {s: [[round(float(v), 1) for v in r] for r in planted[s]] for s in planted},
                "rest_ankle": {s: round(v, 1) for s, v in _rest_ankle.items()}})
    return out


def rest_on_jump():
    """Leave the file as it was found: the jump start active over its frames, and the knee poles following the feet
    as the jump start was made with, since the sprint keys them otherwise."""
    rig = bpy.data.objects["rig"]
    rig.animation_data.action = bpy.data.actions["JumpStart"]
    for side in ("l", "r"):
        rig.pose.bones["c_leg_pole." + side]["pole_parent"] = 1
    scn = bpy.context.scene
    scn.frame_start, scn.frame_end = 0, 15
    scn.render.fps, scn.render.fps_base = 30, 1.0
    scn.frame_set(0)
    return {"action": rig.animation_data.action.name, "pole_parent": [rig.pose.bones["c_leg_pole." + s]["pole_parent"] for s in ("l", "r")]}


def sprint_preview(name, out_dir, loops=3, size=(640, 720), every=2):
    """Key poses of one stride, side and front, and a video of the sprint over a checkered floor moving back at
    SPRINT_SPEED, side and front side by side. On that floor a planted foot stands still and a sliding one moves
    against the squares, which is the slide the numbers measure, made visible. Everything temporary is removed.
    The video takes every `every`th key, at the speed it plays: at 60 keys a second a whole one outran the MCP."""
    g = GAITS[name]
    rig, body = bpy.data.objects["rig"], bpy.data.objects[name]
    H = float(max((body.matrix_world @ Vector(c)).z for c in body.bound_box))
    k = H / HUMAN_HEIGHT
    step = SPRINT_FPS // 30
    n = g["cycle"] * step
    keys = sorted({0, g["contact"] * step // 2, g["contact"] * step, round(0.3 * n), round(0.45 * n), round(0.6 * n),
                   round(0.75 * n), round(0.9 * n)})
    sheet = frames_sheet(name, {"height": H}, out_dir, frames=keys, tag="sprint", views=("side", "front"))
    sc = bpy.context.scene
    prev = (sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.camera, sc.render.filepath,
            sc.frame_current, sc.display.shading.color_type)
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = size
    # The floor: 1 m squares, an image repeating every 2 m, so sliding it by its period is seamless.
    px = 64
    sq = ((np.arange(2 * px)[:, None] // px + np.arange(2 * px)[None, :] // px) % 2).astype(np.float32)
    rgba = np.dstack([0.25 + 0.35 * sq] * 3 + [np.ones_like(sq)])
    img = bpy.data.images.new("SprintFloorChecker", 2 * px, 2 * px)
    img.pixels.foreach_set(rgba.ravel())
    mat = bpy.data.materials.new("SprintFloor")
    mat.use_nodes = True
    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image, tex.interpolation = img, "Closest"
    mat.node_tree.links.new(tex.outputs["Color"], mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"])
    mat.node_tree.nodes.active = tex
    half = 20.0
    me = bpy.data.meshes.new("SprintFloor")
    me.from_pydata([(-half, -half, 0), (half, -half, 0), (half, half, 0), (-half, half, 0)], [], [(0, 1, 2, 3)])
    uv = me.uv_layers.new()
    for loop, (u, v) in zip(uv.data, ((0, 0), (half, 0), (half, half), (0, half))):
        loop.uv = (u, v)
    me.materials.append(mat)
    floor = bpy.data.objects.new("SprintFloor", me)
    sc.collection.objects.link(floor)
    cd = bpy.data.cameras.new("SprintCam")
    cam = bpy.data.objects.new("SprintCam", cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    tmp = os.path.join(out_dir, "_sprint_frames")
    os.makedirs(tmp, exist_ok=True)
    act = rig.animation_data.action
    clip = int(act.frame_range[1] - act.frame_range[0])
    count = loops * clip
    views = {"side": (3.4 * k, 0, 0.55 * H), "front": (0, -3.4 * k, 0.55 * H)}
    paths = []
    for i in range(0, count, every):
        sc.frame_set(i % clip)
        floor.location.y = (SPRINT_SPEED * i / SPRINT_FPS) % 2.0
        row = []
        for view, loc in views.items():
            cam.location = loc
            cam.rotation_euler = (Vector((0, 0, 0.5 * H)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
            path = os.path.join(tmp, "%s_%04d.png" % (view, i))
            sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
            im = bpy.data.images.load(path)
            a = np.empty(im.size[0] * im.size[1] * 4, np.float32)
            im.pixels.foreach_get(a)
            row.append(a.reshape(im.size[1], im.size[0], 4))
            bpy.data.images.remove(im)
            os.remove(path)
        both = np.concatenate(row, 1)
        path = os.path.join(tmp, "pair_%04d.png" % i)
        pair = bpy.data.images.new("sprint_pair", both.shape[1], both.shape[0])
        pair.pixels.foreach_set(both.ravel())
        pair.filepath_raw = path
        pair.file_format = "PNG"
        pair.save()
        bpy.data.images.remove(pair)
        paths.append(path)
    for o, d in ((floor, me), (cam, cd)):
        bpy.data.objects.remove(o)
    bpy.data.meshes.remove(me)
    bpy.data.cameras.remove(cd)
    bpy.data.materials.remove(mat)
    bpy.data.images.remove(img)
    (sc.render.engine, sc.render.resolution_x, sc.render.resolution_y, sc.camera, sc.render.filepath, f0,
     sc.display.shading.color_type) = prev
    sc.frame_set(f0)
    # The frames into an MP4 through the sequencer, since this machine has no ffmpeg of its own.
    video = os.path.join(out_dir, "%s_sprint_preview.mp4" % name)
    edit = bpy.data.scenes.new("SprintPreviewEdit")
    edit.render.resolution_x, edit.render.resolution_y = 2 * size[0], size[1]
    edit.render.resolution_percentage = 100
    edit.render.fps, edit.render.fps_base = SPRINT_FPS // every, 1.0
    se = edit.sequence_editor_create()
    strips = se.strips if hasattr(se, "strips") else se.sequences     # renamed in Blender 4.4; empty is falsy
    strip = strips.new_image("frames", paths[0], 1, 1)
    for p in paths[1:]:
        strip.elements.append(os.path.basename(p))
    edit.frame_start, edit.frame_end = 1, len(paths)
    fmt = edit.render.image_settings
    if hasattr(fmt, "media_type"):
        fmt.media_type = "VIDEO"
    fmt.file_format = "FFMPEG"
    edit.render.ffmpeg.format, edit.render.ffmpeg.codec = "MPEG4", "H264"
    edit.render.ffmpeg.constant_rate_factor = "HIGH"
    edit.render.filepath = video
    with bpy.context.temp_override(scene=edit):
        bpy.ops.render.render(animation=True, scene=edit.name)
    bpy.data.scenes.remove(edit)
    for p in paths:
        os.remove(p)
    os.rmdir(tmp)
    leftover = [o.name for o in bpy.data.objects if o.name.startswith(("SprintFloor", "SprintCam"))]
    return {"sheet": sheet["sheet"], "key_frames": keys, "video": video,
            "video_kb": round(os.path.getsize(video) / 1024) if os.path.exists(video) else None,
            "frames": count, "leftover": leftover}


def sprint_measure(name):
    """Off the evaluated rig and mesh, every frame: how fast each planted ball slides (SPRINT_SPEED within 5%),
    the lowest point of the body (not below the ground), the root (still), and the loop (last frame = first)."""
    rig, body = bpy.data.objects["rig"], bpy.data.objects[name]
    scn = bpy.context.scene
    act = rig.animation_data.action
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    balls, lows, roots, mats, legs = {"l": [], "r": []}, [], [], [], {"l": [], "r": []}
    shape = _run_shape(name)
    shape_err = {"lowerarm": 0.0, "hand": 0.0, "calf": 0.0, "foot": 0.0}
    for f in range(f0, f1 + 1):
        scn.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        for s in ("l", "r"):
            balls[s].append(np.array(rig.matrix_world @ rig.pose.bones["toes_01." + s].head))
        ev = body.evaluated_get(dg)
        m = ev.to_mesh()
        co = np.empty(len(m.vertices) * 3)
        m.vertices.foreach_get("co", co)
        lows.append(float(co.reshape(-1, 3)[:, 2].min()))
        ev.to_mesh_clear()
        roots.append(np.array(rig.pose.bones["c_traj"].matrix.translation))
        t = (f - f0) / max(f1 - f0, 1)
        for s in ("l", "r"):
            pbs = rig.pose.bones
            for joint, bone in (("lowerarm", "c_forearm_fk."), ("hand", "c_hand_fk."), ("calf", "leg_ik."),
                                ("foot", "foot.")):
                off = abs((rig.matrix_world @ pbs[bone + s].head).x - shape(joint + "_" + s, t).x) * 100
                shape_err[joint] = max(shape_err[joint], off)
        for s, sgn in (("l", 1), ("r", -1)):
            pb = rig.pose.bones
            hip, knee, ankle = pb["thigh_ik." + s].head, pb["leg_ik." + s].head, pb["foot." + s].head
            foot_v, toe_v = pb["toes_01." + s].head - ankle, pb["toes_01." + s].tail - pb["toes_01." + s].head
            # How far the knee points out of the leg's plane, as a fraction of how far it stands off the hip-ankle
            # line; a near-straight leg has no direction to point, so it counts as none.
            bow = knee - 0.5 * (hip + ankle)
            legs[s].append(list(_leg_angles(pb, s)) + [math.degrees(foot_v.angle(toe_v)),
                                                        sgn * bow.x / bow.length if bow.length > 0.03 else 0.0])
        mats.append(np.array([np.array(b.matrix) for b in rig.pose.bones if b.bone.use_deform]))
    rest_z = {s: rig.data.bones["toes_01." + s].head_local.z for s in ("l", "r")}
    speeds = []
    for s in ("l", "r"):
        b = np.array(balls[s])
        # Planted means at its rest height to the half millimetre: the frame before a landing comes within 1 mm
        # (the Martian's, once his feet followed the run across the body) and is arriving, not sliding.
        planted = [i for i in range(len(b)) if abs(b[i][2] - rest_z[s]) < 0.0005]
        for i, j in zip(planted, planted[1:]):
            if j == i + 1:
                v = (b[j] - b[i]) * scn.render.fps
                speeds.append((s, f0 + i, round(float(np.hypot(v[0], v[1])), 3), round(float(v[1]), 3)))
    loop = float(np.abs(mats[0] - mats[-1]).max())
    scn.frame_set(f0)
    vals = [v for _, _, v, _ in speeds]
    # Two strides, two feet, each planted for `contact` frame-to-frame steps. Fewer means a foot that was meant to
    # be on the ground was not, which is what an out-of-reach IK target looks like.
    expected = 4 * GAITS[name]["contact"] * round(scn.render.fps / 30)
    # The legs against LIMITS: knee fold, the ankle against its rest angle (positive bends the foot up toward the
    # shin), the boot's bend at the ball against its rest bend, and how far the knee points out of the leg's plane.
    rig.animation_data.action, saved = None, act
    for b in rig.pose.bones:
        b.location, b.rotation_euler, b.rotation_quaternion = (0, 0, 0), (0, 0, 0), (1, 0, 0, 0)
    bpy.context.view_layer.update()
    rest = {}
    for s in ("l", "r"):
        pbr = rig.pose.bones
        a = pbr["foot." + s].head
        rest[s] = (float(_leg_angles(pbr, s)[2]),
                   math.degrees((pbr["toes_01." + s].head - a).angle(pbr["toes_01." + s].tail - pbr["toes_01." + s].head)))
    rig.animation_data.action = saved
    scn.frame_set(f0)
    L = {s: np.array(legs[s]) for s in legs}
    knee_max = max(float(L[s][:, 1].max()) for s in L)
    dorsi = max(float((L[s][:, 2] - rest[s][0]).max()) for s in L)
    plantar = max(float((rest[s][0] - L[s][:, 2]).max()) for s in L)
    toe = max(float((L[s][:, 3] - rest[s][1]).max()) for s in L)
    splay = max(float(np.abs(L[s][:, 4]).max()) for s in L)
    legs_ok = (knee_max <= LIMITS["knee_max"] and dorsi <= LIMITS["dorsiflex_max"]
               and plantar <= LIMITS["plantarflex_max"] and toe <= LIMITS["toe_bend_max"])
    shape_ok = (max(shape_err["lowerarm"], shape_err["hand"], shape_err["calf"]) <= LIMITS["shape_max"]
                and shape_err["foot"] <= LIMITS["ankle_shape_max"])
    return {"planted_speeds": speeds, "speed_min": min(vals) if vals else None, "speed_max": max(vals) if vals else None,
            "planted_steps": len(vals), "planted_expected": expected,
            "speed_ok": len(vals) == expected and all(abs(v - SPRINT_SPEED) <= 0.05 * SPRINT_SPEED for v in vals),
            "legs_ok": legs_ok, "shape_ok": shape_ok,
            "shape_err_cm": {k: round(v, 2) for k, v in shape_err.items()},
            "knee_fold_max": round(knee_max, 1), "ankle_dorsiflex_max": round(dorsi, 1),
            "ankle_plantarflex_max": round(plantar, 1), "toe_bend_max": round(toe, 1), "knee_splay_max": round(splay, 2),
            "lowest_cm": round(100 * min(lows), 2), "lowest_at": f0 + int(np.argmin(lows)),
            "root_moves_m": round(float(np.ptp(np.array(roots), 0).max()), 5), "loop_max_diff": round(loop, 6)}


# --- The falls (201) --------------------------------------------------------------------------------------------
#
# Four deaths, made once on the Humanoid man and played on every race as his jump is (Joe, 18 October): backward
# (shot from the front), forward (from behind, ending face down), and to each side (rolling part way onto the back).
# Epic's mannequin deaths never reach the ground -- they hand over to a ragdoll part way -- and a ragdoll cannot
# finish here: the terrain has no collision. So each of these ends lying, and holds.
#
#     fall("Death_Backward", BACKWARD)     keys every frame from the eased keys below, at 30 a second, in place
#     fall_measure(name)                   the lowest point every frame (never below the ground), the legs' reach
#     fall_sheet(tag, frames, views, dir)  key frames from each view; fall_video(...) the action at its own speed
#     export(name, "Death_Backward", name + "_Death_Backward", maps=False)
#
# fall() lifts the body on any frame where something other than a foot would go below the ground, and each foot by
# its own target, so eased keys cannot sink a hip, a hand or a heel between them; what it lifted, and by which bone
# when over 2 cm, is kept on the action. Arms for a lying pose were found with fall_arm_search, which tries a grid and
# keeps the hand resting lowest with nothing of the arm underground.

FALL_KEYS = ["c_root_master.x", "c_spine_02.x", "c_spine_04.x", "c_neck.x", "c_head.x", "c_shoulder.l", "c_shoulder.r",
             "c_arm_fk.l", "c_arm_fk.r", "c_forearm_fk.l", "c_forearm_fk.r", "c_hand_fk.l", "c_hand_fk.r",
             "c_foot_ik.l", "c_foot_ik.r"]

# A fall, at thirtieths of a second: keys of named values, each eased to the next. lean is the whole body's tip (degrees
# about the hips: lean_x back(-)/forward(+), lean_y to a side); hip_y/hip_z where the hips are (metres); sp2/sp4/neck/head
# bend (degrees about X, + forward); twist turns the chest about Z; turn turns the head about Z; per arm out (from the
# A-pose), swing (+ back), elbow; per foot y, z, toe (- toes up), splay.
DEFAULT = dict(lean_x=0, lean_y=0, hip_x=0, hip_y=0.0, hip_z=0.92, sp2=0, sp4=0, neck=0, head=0, twist=0, turn=0, tilt=0,
               out_l=-12, out_r=-12, swing_l=0, swing_r=0, elbow_l=-8, elbow_r=-8,
               fx_l=0, fx_r=0, fy_l=0.02, fy_r=0.02, fz_l=0.09, fz_r=0.09, toe_l=0, toe_r=0, splay_l=0, splay_r=0, roll_l=0, roll_r=0)

BACKWARD = [
    (0, {}),
    (3, dict(lean_x=-8, hip_y=0.04, hip_z=0.91, sp2=-6, sp4=-14, neck=-6, head=-18, twist=10,
             out_l=45, out_r=30, swing_l=-10, swing_r=-5, elbow_l=-45, elbow_r=-35)),
    (8, dict(lean_x=-12, hip_y=0.08, hip_z=0.86, sp2=-4, sp4=-8, neck=-2, head=-8, twist=12,
             out_l=30, out_r=24, swing_l=0, swing_r=4, elbow_l=-30, elbow_r=-28)),
    (14, dict(lean_x=-22, hip_y=0.16, hip_z=0.58, sp2=4, sp4=2, neck=6, head=12, twist=8, turn=6,
              out_l=20, out_r=16, swing_l=15, swing_r=12, elbow_l=-20, elbow_r=-24, fy_r=-0.04)),
    (20, dict(lean_x=-55, hip_y=0.32, hip_z=0.21, sp2=8, sp4=8, neck=10, head=25, twist=6, turn=10,
              out_l=55, out_r=48, swing_l=8, swing_r=4, elbow_l=-25, elbow_r=-30,
              fy_l=-0.02, fy_r=-0.08, fz_l=0.10, fz_r=0.10, toe_l=-15, toe_r=-12)),
    (25, dict(lean_x=-86, hip_y=0.43, hip_z=0.155, sp2=5, sp4=4, neck=4, head=10, twist=4, turn=16,
              out_l=40, out_r=36, swing_l=30, swing_r=26, elbow_l=-12, elbow_r=-14,
              fy_l=-0.18, fy_r=-0.24, fz_l=0.11, fz_r=0.11, toe_l=-48, toe_r=-50, splay_l=14, splay_r=18)),
    (28, dict(lean_x=-84, hip_y=0.44, hip_z=0.16, sp2=4, sp4=3, neck=-4, head=-12, twist=3, turn=20,
              out_l=32, out_r=28, swing_l=22, swing_r=20, elbow_l=-14, elbow_r=-15,
              fy_l=-0.26, fy_r=-0.30, fz_l=0.115, fz_r=0.115, toe_l=-60, toe_r=-64, splay_l=20, splay_r=22)),
    (33, dict(lean_x=-88, hip_y=0.45, hip_z=0.157, sp2=4, sp4=3, neck=-6, head=-14, twist=3, turn=25,
              out_l=26, out_r=24, swing_l=18, swing_r=18, elbow_l=-15, elbow_r=-16,
              fx_l=0.03, fx_r=-0.05, fy_l=-0.31, fy_r=-0.35, fz_l=0.12, fz_r=0.12, toe_l=-68, toe_r=-72,
              splay_l=24, splay_r=28)),
    (40, None),
]


FORWARD = [
    (0, {}),
    (3, dict(lean_x=5, hip_y=-0.05, hip_z=0.91, sp2=-6, sp4=-14, neck=-8, head=-16, twist=-8,
             out_l=34, out_r=42, swing_l=22, swing_r=28, elbow_l=-35, elbow_r=-45)),
    (8, dict(lean_x=10, hip_y=-0.10, hip_z=0.86, sp2=2, sp4=-2, neck=2, head=6, twist=-10,
             out_l=24, out_r=30, swing_l=10, swing_r=14, elbow_l=-28, elbow_r=-32)),
    (14, dict(lean_x=24, hip_y=-0.16, hip_z=0.56, sp2=8, sp4=8, neck=8, head=16, twist=-6, turn=-8,
              out_l=10, out_r=14, swing_l=-8, swing_r=-4, elbow_l=-20, elbow_r=-24, fy_l=0.04)),
    (20, dict(lean_x=55, hip_y=-0.20, hip_z=0.44, sp2=10, sp4=10, neck=10, head=18, twist=-4, turn=-20,
              out_l=18, out_r=22, swing_l=-20, swing_r=-14, elbow_l=-18, elbow_r=-22,
              fy_l=0.16, fy_r=0.12, fz_l=0.10, fz_r=0.10, toe_l=30, toe_r=26)),
    (25, dict(lean_x=86, hip_y=-0.30, hip_z=0.15, sp2=6, sp4=4, neck=6, head=10, twist=-3, turn=-55,
              out_l=40, out_r=24, swing_l=-16, swing_r=4, elbow_l=-28, elbow_r=-14,
              fy_l=0.46, fy_r=0.43, fz_l=0.11, fz_r=0.11, toe_l=50, toe_r=46, splay_l=8, splay_r=10)),
    (28, dict(lean_x=84, hip_y=-0.31, hip_z=0.15, sp2=5, sp4=3, neck=4, head=6, twist=-3, turn=-65,
              out_l=54, out_r=20, swing_l=-24, swing_r=8, elbow_l=-34, elbow_r=-10,
              fy_l=0.47, fy_r=0.44, fz_l=0.11, fz_r=0.11, toe_l=62, toe_r=58, splay_l=12, splay_r=14)),
    (33, dict(lean_x=88, hip_y=-0.32, hip_z=0.14, sp2=4, sp4=3, neck=4, head=6, twist=-3, turn=-70,
              out_l=60, out_r=20, swing_l=-30, swing_r=10, elbow_l=-40, elbow_r=-10,
              fx_l=0.04, fx_r=-0.03, fy_l=0.50, fy_r=0.47, fz_l=0.12, fz_r=0.12, toe_l=64, toe_r=60,
              splay_l=14, splay_r=16)),
    (40, None),
]

# Shot from the right: the body goes down to its left (+x), tipping onto its side and rolling part way onto its back.
TO_LEFT = [
    (0, {}),
    (3, dict(lean_y=6, hip_x=0.03, hip_z=0.91, sp4=-8, twist=14, neck=-4, head=-10, tilt=14,
             out_l=26, out_r=44, swing_l=-4, swing_r=-12, elbow_l=-30, elbow_r=-48)),
    (8, dict(lean_y=12, lean_x=-4, hip_x=0.08, hip_z=0.86, sp4=-4, twist=16, head=-2, tilt=10,
             out_l=20, out_r=34, swing_l=4, swing_r=-2, elbow_l=-26, elbow_r=-34, fz_l=0.09)),
    (14, dict(lean_y=26, lean_x=-4, hip_x=0.16, hip_z=0.58, sp2=4, sp4=2, twist=12, neck=6, head=10, tilt=6, turn=10,
              out_l=16, out_r=26, swing_l=10, swing_r=6, elbow_l=-20, elbow_r=-26, fx_r=0.04)),
    (20, dict(lean_y=56, lean_x=-12, hip_x=0.30, hip_z=0.24, sp2=6, sp4=6, twist=8, neck=8, head=16, tilt=0, turn=14,
              out_l=5, out_r=40, swing_l=-40, swing_r=-6, elbow_l=-40, elbow_r=-30,
              fx_l=0.02, fx_r=0.08, fy_l=-0.04, fy_r=0.02, fz_l=0.10, fz_r=0.12, roll_l=20, roll_r=-10)),
    (25, dict(lean_y=86, lean_x=-50, hip_x=0.42, hip_z=0.17, sp2=4, sp4=4, twist=5, neck=2, head=4, tilt=-6, turn=18,
              out_l=2, out_r=8, swing_l=-50, swing_r=30, elbow_l=-34, elbow_r=-14,
              fx_l=-0.42, fx_r=-0.12, fy_l=-0.10, fy_r=0.06, fz_l=0.10, fz_r=0.12, toe_l=-20, toe_r=-24,
              roll_l=45, roll_r=-30)),
    (28, dict(lean_y=84, lean_x=-56, hip_x=0.43, hip_z=0.185, sp2=4, sp4=3, twist=4, neck=-2, head=-6, tilt=-8, turn=20,
              out_l=0, out_r=2, swing_l=-56, swing_r=50, elbow_l=-38, elbow_r=-12,
              fx_l=-0.46, fx_r=-0.15, fy_l=-0.12, fy_r=0.08, fz_l=0.10, fz_r=0.11, toe_l=-26, toe_r=-30,
              roll_l=50, roll_r=-36)),
    (33, dict(lean_y=88, lean_x=-60, hip_x=0.44, hip_z=0.18, sp2=4, sp4=3, twist=4, neck=-4, head=-10, tilt=-8, turn=22,
              out_l=0, out_r=0, swing_l=-60, swing_r=60, elbow_l=-40, elbow_r=-10,
              fx_l=-0.48, fx_r=-0.16, fy_l=-0.14, fy_r=0.09, fz_l=0.10, fz_r=0.11, toe_l=-30, toe_r=-32,
              roll_l=52, roll_r=-38)),
    (40, None),
]

_MIRRORED = ("lean_y", "hip_x", "twist", "turn", "tilt")


def mirror(keys):
    """The same fall to the other side: left and right swapped, and everything across the body negated."""
    out = []
    for f, d in keys:
        if d is None:
            out.append((f, None))
            continue
        m = {}
        for k, v in d.items():
            if k.endswith("_l") or k.endswith("_r"):
                other = k[:-2] + ("_r" if k.endswith("_l") else "_l")
                m[other] = -v if k.startswith("fx") else v
            elif k in _MIRRORED:
                m[k] = -v
            else:
                m[k] = v
        out.append((f, m))
    return out


TO_RIGHT = mirror(TO_LEFT)


def _ease(a, b, t):
    t = t * t * (3 - 2 * t)
    return a + (b - a) * t


def _resolve(keys):
    out, prev = [], dict(DEFAULT)
    for f, d in keys:
        cur = dict(prev) if d is None else dict(DEFAULT, **d)
        out.append((f, cur))
        prev = cur
    return out


def _at(keys, f):
    keys = _resolve(keys)
    for (f0, a), (f1, b) in zip(keys, keys[1:]):
        if f0 <= f <= f1:
            t = (f - f0) / (f1 - f0)
            return {k: _ease(a[k], b[k], t) for k in a}
    return keys[-1][1]


def fall_pose(p):
    rig = bpy.data.objects["rig"]
    pb, X, Y, reset, turn, move = _pose_tools(rig)
    Z = Vector((0, 0, 1))
    reset()
    # Limbs first, in the standing frame; the lean then takes them with it.
    turn("c_spine_02.x", X, p["sp2"])
    turn("c_spine_04.x", X, p["sp4"])
    turn("c_spine_04.x", Z, p["twist"])
    turn("c_neck.x", X, p["neck"])
    turn("c_head.x", X, p["head"])
    turn("c_head.x", Z, p["turn"])
    turn("c_head.x", Y, p["tilt"])
    for side, sgn in (("l", 1), ("r", -1)):
        turn("c_arm_fk." + side, Y, -sgn * p["out_" + side])
        turn("c_arm_fk." + side, X, p["swing_" + side])
        turn("c_forearm_fk." + side, X, p["elbow_" + side])
    hip = pb["c_root_master.x"].head.copy()
    # To the side first, then about the body's own length: a body that goes down sideways and rolls onto its back.
    # (Forward and backward falls have no side, so the order changes nothing for them.)
    turn("c_root_master.x", Y, p["lean_y"], pivot=hip)
    turn("c_root_master.x", X, p["lean_x"], pivot=hip)
    move("c_root_master.x", (p["hip_x"] - hip.x, p["hip_y"] - hip.y, p["hip_z"] - hip.z))
    for side, sgn in (("l", 1), ("r", -1)):
        ankle = pb["c_foot_ik." + side].head.copy()
        move("c_foot_ik." + side, (p["fx_" + side], p["fy_" + side] - ankle.y, p["fz_" + side] - ankle.z))
        a = pb["c_foot_ik." + side].head.copy()
        turn("c_foot_ik." + side, X, p["toe_" + side], pivot=a)
        turn("c_foot_ik." + side, Z, sgn * p["splay_" + side], pivot=a)
        turn("c_foot_ik." + side, Y, sgn * p["roll_" + side], pivot=a)


def fall(action, keys):
    rig = bpy.data.objects["rig"]
    pb = rig.pose.bones
    old = bpy.data.actions.get(action)
    if old is not None:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new(action)
    act.use_fake_user = True
    rig.animation_data.action = act
    last = keys[-1][0]
    body = next(o for o in bpy.data.objects if o.type == "MESH" and o.parent == rig)
    names = {g.index: g.name for g in body.vertex_groups}
    owner = [names[max(v.groups, key=lambda g: g.weight).group] if v.groups else "" for v in body.data.vertices]
    feet = np.array([o.startswith(("foot", "toes")) for o in owner])
    foot_of = {s: np.array([o.startswith(("foot", "toes")) and o.endswith("." + s) for o in owner]) for s in ("l", "r")}
    lifts, culprits = [], []
    for f in range(0, last + 1):
        fall_pose(_at(keys, f))
        # Nothing but a foot is let below the ground: the body is lifted by whatever went under, so the eased keys
        # cannot sink a hip or a shoulder between them.
        dg = bpy.context.evaluated_depsgraph_get()
        ev = body.evaluated_get(dg)
        m = ev.to_mesh()
        co = np.empty(len(m.vertices) * 3)
        m.vertices.foreach_get("co", co)
        ev.to_mesh_clear()
        z = co.reshape(-1, 3)[:, 2]
        under = 0.004 - float(z[~feet].min())
        if under > 0.02:
            culprits.append((f, owner[int(np.where(~feet)[0][np.argmin(z[~feet])])], round(100 * under, 1)))
        if under > 0:
            pb["c_root_master.x"].matrix = Matrix.Translation(Vector((0, 0, under))) @ pb["c_root_master.x"].matrix
            bpy.context.view_layer.update()
        # And each foot by its own target, so a heel turning up does not dig in.
        for side in ("l", "r"):
            dg = bpy.context.evaluated_depsgraph_get()
            ev = body.evaluated_get(dg)
            m = ev.to_mesh()
            co = np.empty(len(m.vertices) * 3)
            m.vertices.foreach_get("co", co)
            ev.to_mesh_clear()
            dug = 0.002 - float(co.reshape(-1, 3)[foot_of[side], 2].min())
            if dug > 0:
                c = pb["c_foot_ik." + side]
                c.matrix = Matrix.Translation(Vector((0, 0, dug))) @ c.matrix
                bpy.context.view_layer.update()
                under = max(under, dug)
        lifts.append(round(100 * max(under, 0.0), 1))
        for c in FALL_KEYS:
            pb[c].keyframe_insert("location", frame=f)
            pb[c].keyframe_insert("rotation_euler", frame=f)
        for c in ("c_hand_ik.l", "c_hand_ik.r", "c_foot_ik.l", "c_foot_ik.r"):
            pb[c].keyframe_insert('["ik_fk_switch"]', frame=f)
    scn = bpy.context.scene
    scn.frame_start, scn.frame_end = 0, last
    scn.render.fps, scn.render.fps_base = 30, 1.0
    act["lifts_cm"] = lifts
    act["culprits"] = str(culprits[::4])
    return act


def fall_measure(name):
    """Every frame off the evaluated mesh: the lowest point (not below the ground), the hip-to-ankle reach (not past
    the leg), and at the end how much of the body lies within 3 cm of the ground."""
    rig, body = bpy.data.objects["rig"], bpy.data.objects[name]
    scn = bpy.context.scene
    act = rig.animation_data.action
    lows, reach = [], []
    for f in range(int(act.frame_range[0]), int(act.frame_range[1]) + 1):
        scn.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = body.evaluated_get(dg)
        m = ev.to_mesh()
        co = np.empty(len(m.vertices) * 3)
        m.vertices.foreach_get("co", co)
        ev.to_mesh_clear()
        co = co.reshape(-1, 3)
        lows.append(float(co[:, 2].min()))
        pbs = rig.pose.bones
        reach.append(max((pbs["thigh_ik." + s].head - pbs["foot." + s].head).length for s in ("l", "r")))
        last = co
    touching = float((last[:, 2] < 0.03).mean())
    return {"lowest_cm": round(100 * min(lows), 1), "lowest_at": int(np.argmin(lows)),
            "lowest_final_cm": round(100 * lows[-1], 1), "reach_max": round(max(reach), 3),
            "final_top_cm": round(100 * float(last[:, 2].max()), 1), "touching_final": round(touching, 3)}


def fall_sheet(tag, frames, views, out_dir):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 480, 360
    if "FallGround" not in bpy.data.objects:
        me = bpy.data.meshes.new("FallGround")
        me.from_pydata([(-3, -3, 0), (3, -3, 0), (3, 3, 0), (-3, 3, 0)], [], [(0, 1, 2, 3)])
        g = bpy.data.objects.new("FallGround", me)
        sc.collection.objects.link(g)
    cd = bpy.data.cameras.get("FallCam") or bpy.data.cameras.new("FallCam")
    cam = bpy.data.objects.get("FallCam") or bpy.data.objects.new("FallCam", cd)
    if cam.name not in sc.collection.objects:
        sc.collection.objects.link(cam)
    sc.camera = cam
    rows = []
    for view, loc, target in views:
        row = []
        for f in frames:
            sc.frame_set(f)
            cam.location = loc
            cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
            path = "%s/%s_f%02d_%s.png" % (out_dir, tag, f, view)
            sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
            im = bpy.data.images.load(path)
            a = np.empty(im.size[0] * im.size[1] * 4, np.float32)
            im.pixels.foreach_get(a)
            row.append(a.reshape(im.size[1], im.size[0], 4))
            bpy.data.images.remove(im)
        rows.append(np.concatenate(row, 1))
    g = np.concatenate(rows[::-1], 0)
    sheet = bpy.data.images.new("fall_sheet", g.shape[1], g.shape[0])
    sheet.pixels.foreach_set(g.ravel())
    os.makedirs(out_dir, exist_ok=True)
    out = "%s/%s_sheet.png" % (out_dir, tag)
    sheet.filepath_raw = out
    sheet.file_format = "PNG"
    sheet.save()
    bpy.data.images.remove(sheet)
    return out


def fall_clean_up():
    for n in ("FallGround", "FallCam"):
        o = bpy.data.objects.get(n)
        if o is not None:
            bpy.data.objects.remove(o)
    for coll in (bpy.data.meshes, bpy.data.cameras):
        x = coll.get("FallGround") or coll.get("FallCam")
        if x is not None:
            coll.remove(x)


def fall_video(tag, loc, target, out_dir, size=(640, 400), hold=20):
    """The action as an MP4 at its own 30 a second, the last frame held a moment, for judging the timing."""
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = size
    cam = bpy.data.objects["FallCam"]
    sc.camera = cam
    cam.location = loc
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    f0, f1 = sc.frame_start, sc.frame_end
    sc.frame_end = f1 + hold
    s = sc.render.image_settings
    if hasattr(s, "media_type"):
        s.media_type = "VIDEO"
    s.file_format = "FFMPEG"
    sc.render.ffmpeg.format = "MPEG4"
    sc.render.ffmpeg.codec = "H264"
    sc.render.filepath = "%s/%s.mp4" % (out_dir, tag)
    bpy.ops.render.render(animation=True)
    sc.frame_end = f1
    if hasattr(s, "media_type"):
        s.media_type = "IMAGE"
    s.file_format = "PNG"
    return sc.render.filepath


def fall_arm_search(keys, frame, side, outs, swings, elbows):
    """At one frame of a fall, every arm pose in the grid: how low that arm's hand and the arm itself go. Picks the
    one whose hand rests lowest without anything of the arm under the ground."""
    rig, body = bpy.data.objects["rig"], bpy.data.objects["HumanoidMale"]
    names = {g.index: g.name for g in body.vertex_groups}
    owner = [names[max(v.groups, key=lambda g: g.weight).group] if v.groups else "" for v in body.data.vertices]
    arm = np.array([o.endswith("." + side) and any(k in o for k in ("arm", "hand", "thumb", "index", "middle", "ring",
                                                                       "pinky", "forearm", "shoulder")) for o in owner])
    hand = np.array([o.endswith("." + side) and any(k in o for k in ("hand", "thumb", "index", "middle", "ring", "pinky"))
                     for o in owner])
    rest = [o for o in range(len(owner)) if not arm[o]]
    base = _at(keys, frame)
    rows = []
    for out in outs:
        for swing in swings:
            for elbow in elbows:
                p = dict(base)
                p["out_" + side], p["swing_" + side], p["elbow_" + side] = out, swing, elbow
                fall_pose(p)
                dg = bpy.context.evaluated_depsgraph_get()
                ev = body.evaluated_get(dg)
                m = ev.to_mesh()
                co = np.empty(len(m.vertices) * 3)
                m.vertices.foreach_get("co", co)
                ev.to_mesh_clear()
                z = co.reshape(-1, 3)[:, 2]
                rows.append((round(100 * float(z[arm].min()), 1), round(100 * float(z[hand].min()), 1),
                             round(100 * float(z[rest].min()), 1), out, swing, elbow))
    ok = [r for r in rows if r[0] >= -0.5]
    ok.sort(key=lambda r: r[1])
    return {"best": ok[:5], "tried": len(rows), "body_low_cm": rows[0][2]}
