#!/usr/bin/env python3
"""
Measure a character's rigging landmarks off its mesh: Auto-Rig Pro's Smart markers, and the numbers 175's
two weight corrections need. Task 182, so the Martian, elf and orc men are rigged the way the Humanoid man
was, by measurement rather than by eye.

    blender --factory-startup --background NAME.blend --python tools/characters/measure_landmarks.py -- --name NAME [--render DIR]

Prints one MARKS line of JSON. With --render, also draws the markers on the body, front and side, to check by
eye before anything is rigged from them.

The body stands as tripo_to_blend.py leaves it: facing -Y, soles at z=0, x on his left.
  * root: the hip joints, above the crotch, the lowest height at which the legs' slice closes across the middle;
  * armpit and torso edge: the highest height at which, on his left, a gap still parts the arm from the torso,
    and the torso's outer edge just under it;
  * shoulder: inside the deltoid's round, a fixed fraction of the way from the armpit to the shoulder's top;
  * neck: the base of the neck, where the slice round the middle stops being the shoulders' width;
  * chin: the underside of the chin, where the front of the midline steps forward going up;
  * hand: the wrist, the arm's narrowest section sliced across its own axis, since an A-pose arm slants and a
    level slice would cut it on the diagonal;
  * foot: the ankle, the leg's middle at 5% of the height.
The fractions that are judgement, not geometry, were set on the Humanoid man, whose markers were placed by
hand and checked by posing (175): measured the same way, he must come back within a centimetre of them.
"""

import json
import sys

import bpy
import numpy as np

# Set by the Humanoid man's hand-placed markers (175): neck 1.470, chin 1.535, shoulder (0.175, 1.430),
# root 0.920, wrist (0.453, -0.053, 1.004), ankle (0.170, 0.02, 0.090), armpit 1.245, torso edge 0.18.
ROOT_ABOVE_CROTCH = 0.0711      # of the height: his hip joints sit 12.8 cm above where his legs meet
SHOULDER_UP = 0.773              # of the way from the armpit to the shoulder's top
SHOULDER_IN = 0.951              # of the torso edge
NECK_ABOVE_BASE = -0.0133        # of the height: the collar seal's bottom, 2.4 cm under where the slice narrows
ARMPIT_FOR_WEIGHTS = -0.0083     # of the height: the weight fade's armpit, 1.5 cm under where the gap closes
WRIST_PAST_CUFF = 0.0            # of the arm's length past the cuff's edge: his hand-placed wrist is at it, within 1.2 cm
TORSO_EDGE_FOR_WEIGHTS = 0.978   # of the measured torso edge
ANKLE = 0.05                     # of the height


def coords(obj):
    me = obj.data
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    m = np.array(obj.matrix_world)
    return co @ m[:3, :3].T + m[:3, 3]


def vertex_colours(obj):
    """Each vertex's base colour, read from the packed texture at one of its corners' UVs."""
    me = obj.data
    img = next(n.image for m in me.materials if m and m.use_nodes for n in m.node_tree.nodes
               if n.type == "TEX_IMAGE" and n.image and n.image.name.endswith("_BaseColor"))
    w, h = img.size
    px = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(h, w, 4)[..., :3]
    uv = np.empty(len(me.loops) * 2)
    me.uv_layers.active.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    lv = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get("vertex_index", lv)
    per = np.zeros((len(me.vertices), 2))
    per[lv] = uv
    x = np.clip((per[:, 0] % 1.0) * w, 0, w - 1).astype(int)
    y = np.clip((per[:, 1] % 1.0) * h, 0, h - 1).astype(int)
    return px[y, x]


def triangles(obj):
    me = obj.data
    me.calc_loop_triangles()
    tv = np.empty(len(me.loop_triangles) * 3, np.int32)
    me.loop_triangles.foreach_get("vertices", tv)
    return coords(obj)[tv.reshape(-1, 3)]


def band(co, z, h):
    return co[np.abs(co[:, 2] - z) < h]


class Slicer:
    """Exact horizontal cross-sections. A band of vertices is no use for finding gaps: Tripo's edges are
    5-8 mm long, so a thin band shows gaps that are only the spacing between vertices, inside the torso at
    nearly every height (182). Each triangle the plane crosses gives a segment; their x spans, merged, are
    the section's coverage, with a gap only where the body really has one."""

    def __init__(self, tris):
        self.T = tris
        self.zmin, self.zmax = tris[:, :, 2].min(1), tris[:, :, 2].max(1)

    def segments(self, z):
        T = self.T[(self.zmin < z) & (self.zmax > z)]
        pts = []
        for i, j in ((0, 1), (1, 2), (2, 0)):
            a, b = T[:, i], T[:, j]
            da, db = a[:, 2] - z, b[:, 2] - z
            ok = (da * db) < 0
            t = np.where(ok, da / np.where(ok, da - db, 1.0), 0.0)
            p = a + (b - a) * t[:, None]
            pts.append(np.where(ok[:, None], p, np.nan))
        P = np.stack(pts, 1)                                  # (n, 3 edges, xyz), two of them crossing
        return P

    def x_spans(self, z, side=0):
        """Merged x coverage of the section, as a sorted list of (x0, x1). side 1 keeps x > 0 only."""
        P = self.segments(z)
        if len(P) == 0:
            return []
        x0, x1 = np.nanmin(P[:, :, 0], 1), np.nanmax(P[:, :, 0], 1)
        if side > 0:
            keep = x1 > 0
            x0, x1 = np.maximum(x0[keep], 0.0), x1[keep]
        order = np.argsort(x0)
        spans = []
        for a, b in zip(x0[order], x1[order]):
            if spans and a <= spans[-1][1] + 5e-4:
                spans[-1][1] = max(spans[-1][1], b)
            else:
                spans.append([a, b])
        return [(float(a), float(b)) for a, b in spans]

    def points(self, z):
        P = self.segments(z).reshape(-1, 3)
        return P[~np.isnan(P[:, 0])]


def span_at(spans, x):
    return next((s for s in spans if s[0] - 1e-4 <= x <= s[1] + 1e-4), None)


def measure(obj):
    co = coords(obj)
    sl = Slicer(triangles(obj))
    H = float(co[:, 2].max())
    step = 0.002
    out = {"height": round(H, 4)}

    # crotch: the lowest height from which the section closes across the middle
    crotch = next((float(z) for z in np.arange(0.30 * H, 0.62 * H, step) if span_at(sl.x_spans(z), 0.0)), None)
    if crotch is None:
        raise SystemExit("measure_landmarks: the legs never meet below 0.62 H; is this a body standing at z=0?")
    out["crotch"] = round(crotch, 3)
    root = crotch + ROOT_ABOVE_CROTCH * H

    # armpit: going up his left side, the last height at which a gap parts the arm from the torso
    armpit, edges = None, []
    for z in np.arange(0.45 * H, 0.86 * H, step):
        spans = sl.x_spans(z, side=1)
        torso = span_at(spans, 0.0)
        if torso is None:
            continue
        beyond = [sp for sp in spans if sp[0] > torso[1]]
        if beyond and beyond[0][0] - torso[1] > 0.003:
            armpit = float(z)
            edges.append((float(z), torso[1]))
        elif armpit is not None:
            break
    if armpit is None:
        raise SystemExit("measure_landmarks: no gap between arm and torso on the left; the arms may be fused")
    torso_edge = max(e for z, e in edges if z > armpit - 0.03)
    out["armpit"], out["torso_edge"] = round(armpit, 3), round(torso_edge, 3)

    # shoulder: the top of the shoulder over the torso edge, and a fraction of the way up from the armpit
    sx = SHOULDER_IN * torso_edge
    shoulder_top = max(float(z) for z in np.arange(armpit, 0.95 * H, step) if span_at(sl.x_spans(z, side=1), sx))
    out["shoulder_top"] = round(shoulder_top, 3)
    shoulder = (sx, 0.0, armpit + SHOULDER_UP * (shoulder_top - armpit))

    # neck: from the shoulder's top up, the section round the middle narrows from the shoulders to the neck
    widths = []
    for z in np.arange(shoulder_top - 0.02 * H, 0.97 * H, step):
        sp = span_at(sl.x_spans(z), 0.0)
        if sp:
            widths.append((float(z), sp[1] - sp[0]))
    w = np.array(widths)
    neck_min = float(w[: max(5, len(w) // 3), 1].min())
    base = next(float(z) for z, wd in w if wd < 1.5 * neck_min)
    neck_z = base + NECK_ABOVE_BASE * H
    out["neck_width"], out["neck_base"] = round(neck_min, 3), round(base, 3)

    # chin: up the midline's front, the biggest step forward within the next 15% of the height
    prof = []
    for z in np.arange(neck_z, neck_z + 0.15 * H, step):
        pts = sl.points(z)
        m = pts[np.abs(pts[:, 0]) < 0.015]
        if len(m):
            prof.append((float(z), float(m[:, 1].min())))
    p = np.array(prof)
    drop = p[4:, 1] - p[:-4, 1]                         # over 8 mm of height; forward is -y
    k = int(np.argmin(drop))
    chin = (0.0, float(p[k + 4, 1]), float(p[k + 2, 0]))

    # wrist: sliced across the arm's own axis, the sleeve narrows, flares into the cuff, and narrows again at
    # the wrist before the palm widens. The wrist is that second narrowing, as on the Humanoid man; with no
    # cuff to flare, it is the sleeve's narrowest.
    sh = np.array(shoulder)
    hand_side = co[(co[:, 0] > torso_edge + 0.06 * H) & (co[:, 2] > 0.25 * H)]
    tip = hand_side[np.argmin(hand_side[:, 2])]
    L = float(np.linalg.norm(tip - sh))
    axis = (tip - sh) / L
    arm = co[(co[:, 0] > torso_edge) & (co[:, 2] < armpit + 0.05)]
    t = (arm - sh) @ axis
    perp = arm - sh - np.outer(t, axis)
    # Only what is near the arm's axis: the elf's hands hang beside his thighs, and slices taking in a thigh read
    # 15-20 cm "radii" and put his wrist in his palm, where finger detection failed (182).
    near = np.linalg.norm(perp, axis=1) < 0.13 * H / 1.8
    t, perp = t[near], perp[near]
    # The cuff's edge, by colour: every suit's sleeve differs from the hand it ends at (navy and skin, terracotta
    # and pallor, green and pink, grey and green). Thickness alone put the orc's wrist at his elbow, his forearm
    # being wider than it (182). Each slice's colour is its texels' median; the edge is the first slice nearer the
    # hand's colour than the sleeve's, and the wrist sits WRIST_PAST_CUFF of the arm beyond it.
    col = vertex_colours(obj)[(co[:, 0] > torso_edge) & (co[:, 2] < armpit + 0.05)][near]
    prof = []
    for tt in np.arange(0.40 * L, 0.98 * L, 0.01 * L):
        s_ = np.abs(t - tt) < 0.006
        if s_.sum() < 12:
            continue
        c = perp[s_].mean(0)
        prof.append((float(tt), float(np.linalg.norm(perp[s_] - c, axis=1).mean()), sh + axis * tt + c, np.median(col[s_], 0)))
    ts = np.array([q[0] for q in prof]) / L
    sleeve = np.median(np.array([q[3] for q in prof if 0.45 <= q[0] / L <= 0.55]), 0)
    hand = np.median(np.array([q[3] for q in prof if q[0] / L >= 0.90]), 0)
    if np.linalg.norm(sleeve - hand) < 0.15:
        raise SystemExit("measure_landmarks: sleeve %s and hand %s are the same colour; no cuff to find" % (sleeve, hand))
    edge = next(q[0] for q in prof if q[0] / L > 0.55 and np.linalg.norm(q[3] - hand) < np.linalg.norm(q[3] - sleeve))
    target = edge + WRIST_PAST_CUFF * L
    wrist_i = int(np.argmin(np.abs(ts * L - target)))
    how = "past the cuff"
    wrist = tuple(float(v) for v in prof[wrist_i][2])
    r = np.array([q[1] for q in prof])
    i_f = wrist_i
    i_c = wrist_i
    # Auto-Rig Pro finds the wrist by a ray from the front through the marker, so the marker has to be inside the
    # arm as the front view sees it. The slice's middle need not be: on the Martian's thin wrist it fell just
    # outside, and detection stopped with "marker out of mesh" (182). Centre it in the arm's section at that height.
    spans = sl.x_spans(wrist[2], side=1)
    span = min(spans, key=lambda sp: 0.0 if sp[0] <= wrist[0] <= sp[1] else min(abs(sp[0] - wrist[0]), abs(sp[1] - wrist[0])))
    sec = sl.points(wrist[2])
    sec = sec[(sec[:, 0] >= span[0]) & (sec[:, 0] <= span[1])]
    wrist = ((span[0] + span[1]) / 2, float((sec[:, 1].min() + sec[:, 1].max()) / 2), wrist[2])
    out["wrist"] = {"from": how, "t_over_L": round(prof[wrist_i][0] / L, 3), "cuff_edge_over_L": round(edge / L, 3),
                    "radius": round(prof[wrist_i][1], 4), "sleeve_rgb": [round(float(v), 2) for v in sleeve],
                    "hand_rgb": [round(float(v), 2) for v in hand]}
    out["arm_length"] = round(L, 3)

    # ankle: the left leg's middle at 5% of the height
    pts = sl.points(ANKLE * H)
    leg = pts[pts[:, 0] > 0.01]
    foot = ((leg[:, 0].min() + leg[:, 0].max()) / 2, (leg[:, 1].min() + leg[:, 1].max()) / 2, ANKLE * H)

    out["weights"] = {"armpit": round(armpit + ARMPIT_FOR_WEIGHTS * H, 3), "torso_edge": round(TORSO_EDGE_FOR_WEIGHTS * torso_edge, 3)}
    out["markers"] = {k: [round(float(v), 3) for v in p] for k, p in (
        ("neck", (0.0, 0.0, neck_z)), ("chin", chin), ("shoulder", shoulder), ("hand", wrist),
        ("root", (0.0, 0.0, root)), ("foot", foot))}
    return out


def render_markers(obj, marks, out_dir, name):
    """Front and side, orthographic, with a small sphere on each marker and its mirror."""
    sc = bpy.context.scene
    for o in list(sc.objects):
        if o.type in {"LIGHT", "CAMERA"}:
            bpy.data.objects.remove(o)
    mat = bpy.data.materials.new("Marker")
    mat.diffuse_color = (1.0, 0.1, 0.6, 1.0)
    for part, p in marks["markers"].items():
        for sgn in ((1, -1) if abs(p[0]) > 1e-4 else (1,)):
            bpy.ops.mesh.primitive_uv_sphere_add(radius=0.012 * marks["height"] / 1.8, location=(sgn * p[0], p[1], p[2]))
            s = bpy.context.active_object
            s.data.materials.append(mat)
            s.show_in_front = True
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "FLAT"
    sc.display.shading.color_type = "MATERIAL"
    sc.render.resolution_x, sc.render.resolution_y = 700, 900
    cd = bpy.data.cameras.new("C")
    cd.type = "ORTHO"
    cd.ortho_scale = marks["height"] * 1.1
    cam = bpy.data.objects.new("C", cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    H = marks["height"]
    for view, loc, rot in (("front", (0, -6, H / 2), (np.pi / 2, 0, 0)), ("side", (6, 0, H / 2), (np.pi / 2, 0, np.pi / 2))):
        cam.location, cam.rotation_euler = loc, rot
        sc.render.filepath = "%s/%s_markers_%s.png" % (out_dir, name, view)
        bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    name = argv[argv.index("--name") + 1]
    marks = measure(bpy.data.objects[name])
    print("MARKS " + json.dumps(marks))
    if "--render" in argv:
        render_markers(bpy.data.objects[name], marks, argv[argv.index("--render") + 1], name)


if __name__ == "__main__":
    main()
