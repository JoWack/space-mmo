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

Every number that was the Humanoid man's is kept as his, with the scale it was taken at: k is the body's height
over his 1.80 m, kl its root height over his 0.92 m.
"""

import contextlib
import io
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


def frames_sheet(name, marks, out_dir, frames=(0, 3, 7, 13), tag="jumpstart"):
    """The current action at the given frames, three-quarter and side, in one sheet: the key poses Joe sees."""
    rig = bpy.data.objects["rig"]
    H = marks["height"]
    k = H / HUMAN_HEIGHT
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
    for view, loc in (("side", (3.4 * k, 0, 0.55 * H)), ("34", (2.2 * k, -2.4 * k, 0.6 * H))):
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


def export(name):
    """The FBX and the four maps into client/RawContent/Characters/<name>/, with 175's settings."""
    scn = bpy.context.scene
    rig, body = bpy.data.objects["rig"], bpy.data.objects[name]
    out_dir = RAW + name + "/"
    os.makedirs(out_dir, exist_ok=True)
    applied = {}
    for key, v in EXPORT.items():
        if not hasattr(scn, key):
            applied[key] = "MISSING"
            continue
        setattr(scn, key, v)
        applied[key] = getattr(scn, key)
    fbx = out_dir + name + ".fbx"
    buf = io.StringIO()
    t0 = time.time()
    with contextlib.redirect_stdout(buf), _view3d():
        _object_mode()
        _select(body, rig, active=rig)
        ret = list(bpy.ops.arp.arp_export_fbx_panel("EXEC_DEFAULT", filepath=fbx))
    maps = {}
    for role in ("BaseColor", "Normal", "Roughness", "Metallic"):
        img = bpy.data.images["T_%s_%s" % (name, role)]
        path = out_dir + "T_%s_%s.png" % (name, role)
        img.filepath_raw = path
        img.file_format = "PNG"
        img.save()
        maps[role] = [img.colorspace_settings.name, list(img.size), round(os.path.getsize(path) / 1e6, 2)]
    return {"ret": ret, "seconds": round(time.time() - t0, 1), "fbx": fbx, "fbx_kb": round(os.path.getsize(fbx) / 1024) if os.path.exists(fbx) else None,
            "missing_settings": [key for key, v in applied.items() if v == "MISSING"], "maps": maps,
            "flagged": _flagged(buf.getvalue())}
