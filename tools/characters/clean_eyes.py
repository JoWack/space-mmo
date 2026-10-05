#!/usr/bin/env python3
"""
Clean up the eyes Tripo paints: tone down the orange-red it puts round them, and repaint both irises
the same, centred in the openings the texture already has. Task 180, for the Humanoid man.

    blender --factory-startup --background --python-exit-code 1 <NAME>.blend \
        --python tools/characters/clean_eyes.py -- --name NAME [--iris R,G,B] [--dry-run]

--iris is the iris colour, sRGB 0-1. The default is the sheets' grey for the Humanoid man.
--dry-run measures and reports, and saves nothing.

Tripo's atlas cuts a face into dozens of small islands, so nothing here works in texture space alone.
Every texel of the triangles round the eyes is given its point on the mesh, and the work is done by where
a texel sits on the head:
  * the eye openings are the white and the iris-coloured texels near each eye, measured off the mesh;
  * the ring is skin within an ellipse round each opening, pulled toward the colour of the skin just
    outside it, by how much redder and more orange it is than that skin;
  * the irises are painted from scratch: pupil, iris, darker rim and one catchlight, the same for both.
The change is carried a few texels into the atlas's gutters, so filtering cannot bring the old colour back
at an island's edge.

Re-runnable. The first run keeps Tripo's texture as T_<NAME>_BaseColor_Tripo, on a copy of the material
that the hidden Source object uses, and every run starts from that copy rather than from its own output.
"""

import sys

import bpy
import numpy as np

EYE_Z_GUESS = 1.645          # metres; refined from the texture
EYE_X_GUESS = 0.033
RING_RX, RING_UP, RING_DOWN = 0.027, 0.014, 0.021     # ellipse round each opening, metres
GUTTER_PX = 3
# The whites read as lit from inside in the Capital's night (Joe's playtest, 4 October). Darker ones,
# 0.76 with shadow from both lids, were tried that day and dropped: Tripo's own white past the opening
# (below) showed as a bright rim round them. They stay this light until the face has the texels to lose
# that edge.
SCLERA = np.array([0.86, 0.84, 0.82])
PUPIL = np.array([0.06, 0.06, 0.07])
CATCHLIGHT = np.array([0.96, 0.96, 0.95])


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    def flag(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default

    iris = tuple(float(c) for c in flag("--iris", "0.42,0.41,0.40").split(","))
    return {"name": flag("--name"), "iris": iris, "dry_run": "--dry-run" in argv}


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def rasterize(T, P, W, H, idx):
    """Texel centres inside each triangle idx (UVs T, positions P). Returns rows, cols, 3D points."""
    rows, cols, pts = [], [], []
    for i in idx:
        t = T[i] * (W, H)
        x0, y0 = np.floor(t.min(0) - 0.5).astype(int)
        x1, y1 = np.ceil(t.max(0) - 0.5).astype(int)
        gx, gy = np.meshgrid(np.arange(max(x0, 0), min(x1, W - 1) + 1), np.arange(max(y0, 0), min(y1, H - 1) + 1))
        px, py = gx.ravel() + 0.5, gy.ravel() + 0.5
        (ax, ay), (bx, by), (cx, cy) = t
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-12:
            continue
        l1 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / d
        l2 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / d
        l3 = 1 - l1 - l2
        inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
        if not inside.any():
            continue
        lam = np.stack([l1[inside], l2[inside], l3[inside]], 1)
        rows.append(gy.ravel()[inside])
        cols.append(gx.ravel()[inside])
        pts.append(lam @ P[i])
    if not rows:
        return np.zeros(0, int), np.zeros(0, int), np.zeros((0, 3))
    return np.concatenate(rows), np.concatenate(cols), np.concatenate(pts)


def coverage(T, W, H):
    """Every texel any triangle covers: what the gutter dilation must not touch."""
    cov = np.zeros((H, W), bool)
    r, c, _ = rasterize(T, np.zeros((len(T), 3, 3)), W, H, range(len(T)))
    cov[r, c] = True
    return cov


def ycbcr(rgb):
    y = rgb @ np.array([0.299, 0.587, 0.114])
    return y, (rgb[:, 2] - y) * 0.564, (rgb[:, 0] - y) * 0.713


def rgb_from(y, cb, cr):
    r = y + cr / 0.713
    b = y + cb / 0.564
    g = (y - 0.299 * r - 0.114 * b) / 0.587
    return np.clip(np.stack([r, g, b], 1), 0.0, 1.0)


def classify(rgb):
    v = rgb.max(1)
    s = (v - rgb.min(1)) / np.maximum(v, 1e-6)
    r, g, b = rgb.T
    sclera = (v > 0.60) & (s < 0.28)
    iris = ~sclera & (((b >= r * 0.95) & (v < 0.75)) | ((s < 0.18) & (v < 0.62)))
    return sclera, iris


def main():
    args = parse_args()
    name = args["name"]
    obj = bpy.data.objects[name]
    me = obj.data
    work = bpy.data.images["T_%s_BaseColor" % name]
    W, H = work.size
    keep_name = "T_%s_BaseColor_Tripo" % name
    keep = bpy.data.images.get(keep_name)
    orig = np.empty(W * H * 4, np.float32)
    if keep is None:
        work.pixels.foreach_get(orig)
        # A new image packs what it was generated as -- black -- unless told its pixels changed. That
        # once handed this script a black "original", and it painted eyes over the whole face.
        keep = bpy.data.images.new(keep_name, W, H, alpha=True)
        keep.colorspace_settings.name = work.colorspace_settings.name
        keep.pixels.foreach_set(orig)
        keep.update()
        keep.pack()
        check = np.empty_like(orig)
        keep.pixels.foreach_get(check)
        if np.abs(check - orig).max() > 1.5 / 255:
            sys.exit("clean_eyes: the copy of Tripo's texture did not keep its pixels; nothing saved")
        src = bpy.data.objects.get(name + "_TripoSource")      # its own mesh, sharing the material until now
        if src is not None and src.data.materials and src.data.materials[0]:
            mat = src.data.materials[0].copy()
            mat.name = "MAT_%s_TripoSource" % name
            for n in mat.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image == work:
                    n.image = keep
            src.data.materials[0] = mat
    else:
        keep.pixels.foreach_get(orig)
    orig = orig.reshape(H, W, 4)
    if orig[..., :3].mean() < 0.05:
        sys.exit("clean_eyes: Tripo's texture reads as black; nothing saved")
    out = orig.copy()

    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    tv = np.empty(n * 3, np.int32); me.loop_triangles.foreach_get("vertices", tv); tv = tv.reshape(n, 3)
    tl = np.empty(n * 3, np.int32); me.loop_triangles.foreach_get("loops", tl); tl = tl.reshape(n, 3)
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    uv = np.empty(len(me.loops) * 2); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    P, T = co[tv], uv[tl]
    c = P.mean(1)
    near = np.nonzero((np.abs(c[:, 0]) < 0.08) & (c[:, 2] > 1.58) & (c[:, 2] < 1.72) & (c[:, 1] < -0.04))[0]
    rows, cols, pts = rasterize(T, P, W, H, near)
    rgb = orig[rows, cols, :3]
    sclera, iris = classify(rgb)

    report, eyes = {}, []
    for side, sgn in (("right", -1), ("left", 1)):   # the character's own sides; his right is -X
        sel = (np.sign(pts[:, 0]) == sgn)
        dx, dz = pts[:, 0] - sgn * EYE_X_GUESS, pts[:, 2] - EYE_Z_GUESS
        opening = sel & (sclera | iris) & ((dx / 0.022) ** 2 + (dz / 0.012) ** 2 < 1)
        ox, oz = pts[opening, 0], pts[opening, 2]
        x_lo, x_hi = np.percentile(ox, [2, 98]); z_lo, z_hi = np.percentile(oz, [2, 98])
        cx, cz = (x_lo + x_hi) / 2, (z_lo + z_hi) / 2
        half_w, half_h = (x_hi - x_lo) / 2, (z_hi - z_lo) / 2
        report[side] = {"centre_mm": [round(1000 * float(cx), 1), round(1000 * float(cz), 1)],
                        "opening_mm": [round(2000 * float(half_w), 1), round(2000 * float(half_h), 1)],
                        "texels": int(opening.sum())}
        # An opening the size of the search ellipse means the colours were not told apart: refuse,
        # rather than paint an eye over the face. A real one here is about 200 texels: Tripo spreads one
        # 2048 texture over the whole body, about a texel per square millimetre of face.
        if not (0.015 < 2 * half_w < 0.034 and 0.005 < 2 * half_h < 0.016) or opening.sum() < 60:
            sys.exit("clean_eyes: the %s eye's opening measured %s mm from %d texels, which is no eye; "
                     "nothing saved" % (side, report[side]["opening_mm"], opening.sum()))
        eyes.append((side, sel, opening, cx, cz, half_w, half_h))

    # one iris for both eyes, sized and placed from the two openings together
    mean_w = np.mean([2 * e[5] for e in eyes])
    mean_h = np.mean([e[6] for e in eyes])
    r_iris = float(np.clip(0.2 * mean_w, 0.0045, 0.006))
    report["iris_radius_mm"] = round(1000 * r_iris, 2)

    for side, sel, opening, cx, cz, half_w, half_h in eyes:
        # --- the ring: skin round the opening, pulled toward the skin just outside it
        ex = (pts[:, 0] - cx) / RING_RX
        ez = (pts[:, 2] - cz) / np.where(pts[:, 2] > cz, RING_UP, RING_DOWN)
        e = np.sqrt(ex ** 2 + ez ** 2)
        skin = sel & ~sclera & ~iris & ~opening
        ref_sel = skin & (e > 1.25) & (e < 1.9) & (pts[:, 2] < cz + 0.010)
        if ref_sel.sum() < 100:
            sys.exit("clean_eyes: only %d skin texels round the %s eye to take a colour from; nothing saved"
                     % (ref_sel.sum(), side))
        y, cb, cr = ycbcr(rgb)
        y_ref, cb_ref, cr_ref = np.median(y[ref_sel]), np.median(cb[ref_sel]), np.median(cr[ref_sel])
        ring = skin & (e < 1.0)
        excess = (cr - cr_ref) + 0.5 * (cb_ref - cb)
        w = 0.85 * (1 - smoothstep(0.55, 1.0, e)) * smoothstep(0.0, 0.05, excess)
        w = np.where(ring, w, 0.0)
        y2 = y + w * 0.6 * np.maximum(0.0, (y_ref - 0.04) - y)
        fixed = rgb_from(y2, cb + w * (cb_ref - cb), cr + w * (cr_ref - cr))
        m = w > 0
        out[rows[m], cols[m], :3] = fixed[m]
        report[side].update({"ring_texels": int(m.sum()), "ring_mean_excess": round(float(excess[ring].mean()), 4),
                             "ref_skin_rgb": [round(float(v), 3) for v in np.median(rgb[ref_sel], 0)]})

        # --- the iris, painted from scratch inside the opening
        ix, iz = cx, cz + 0.12 * mean_h
        dx, dz = pts[:, 0] - ix, pts[:, 2] - iz
        d = np.sqrt(dx ** 2 + dz ** 2) / r_iris
        # Every transition is at least a texel wide (about a millimetre here, a quarter of the iris's
        # radius): narrower ones come out as saw-teeth.
        iris_rgb = np.array(args["iris"])
        col = iris_rgb * (1.12 - 0.3 * np.minimum(d, 1.0))[:, None]          # lighter toward the pupil
        rim = smoothstep(0.66, 0.92, d)
        col = col * (1 - rim)[:, None] + (iris_rgb * 0.5) * rim[:, None]
        white = smoothstep(0.90, 1.15, d)                                    # a soft edge into the white
        col = col * (1 - white)[:, None] + SCLERA * white[:, None]
        pupil = 1 - smoothstep(0.24, 0.48, d)
        col = col * (1 - pupil)[:, None] + PUPIL * pupil[:, None]
        catch = 1 - smoothstep(0.12, 0.34, np.sqrt((dx / r_iris - 0.32) ** 2 + (dz / r_iris - 0.36) ** 2))
        col = col * (1 - catch)[:, None] + CATCHLIGHT * catch[:, None]
        lid = smoothstep(cz + 0.30 * half_h, cz + 1.0 * half_h, pts[:, 2])   # the upper lid's shadow
        corner = smoothstep(0.55, 1.0, np.abs(pts[:, 0] - cx) / half_w)
        col = col * (1 - 0.35 * lid - 0.25 * corner)[:, None]
        # Paint the whole almond the opening measured, not just the texels whose colour said "eye":
        # Tripo's dark wedge in his right eye partly reads as skin, and survived the first attempt.
        u = (pts[:, 0] - cx) / half_w
        lens = half_h * np.clip(1 - u ** 2, 0.0, 1.0) ** 0.6                 # the opening's half-height at u
        v = (pts[:, 2] - cz) / np.maximum(lens, 1e-6)
        # Tripo's white runs a little past the opening this measures, and shows as a light edge under the
        # eyes at close range in the Capital's night (Joe's playtest, 4 October). Three ways of covering it
        # were tried and each left a visible ring at a texel a millimetre: stopping the paint short of the
        # lower lid (it uncovered more white), recolouring bright texels outside (a dotted ring), and fading
        # into a lid tone past the edge (a dark outline with the white inside it). This edge stays; the fix
        # is more texels on the face, not more paint (177).
        alpha = (1 - smoothstep(0.85, 1.05, np.abs(u))) * (1 - smoothstep(0.8, 1.1, np.abs(v)))
        alpha = np.where(sel, alpha, 0.0)
        a = alpha > 0
        blend = out[rows[a], cols[a], :3] * (1 - alpha[a])[:, None] + col[a] * alpha[a][:, None]
        out[rows[a], cols[a], :3] = np.clip(blend, 0, 1)
        report[side]["painted_texels"] = int(a.sum())

    # carry the change into the gutters, never into another island
    delta = out[..., :3] - orig[..., :3]
    changed = np.abs(delta).sum(2) > 1e-6
    cov = coverage(T, W, H)
    for _ in range(GUTTER_PX):
        acc = np.zeros_like(delta)
        cnt = np.zeros(changed.shape, np.float32)
        for oy, ox in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            sh = np.roll(np.roll(changed, oy, 0), ox, 1)
            acc += np.where(sh[..., None], np.roll(np.roll(delta, oy, 0), ox, 1), 0)
            cnt += sh
        grow = (cnt > 0) & ~changed & ~cov
        delta[grow] = acc[grow] / cnt[grow][:, None]
        changed |= grow
    out[..., :3] = np.clip(orig[..., :3] + delta, 0, 1)
    report["gutter_texels"] = int((changed & ~cov).sum())
    report["texels_changed"] = int(changed.sum())
    print("EYES " + repr(report))
    if args["dry_run"]:
        print("EYES dry run: nothing saved")
        return
    work.pixels.foreach_set(out.ravel())
    work.update()
    work.pack()
    bpy.ops.wm.save_mainfile()
    print("EYES saved %s" % bpy.data.filepath)


if __name__ == "__main__":
    main()
