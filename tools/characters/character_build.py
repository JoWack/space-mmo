"""
Build a game-ready, rig-ready character mesh from a measured turnaround (task 174's four views).

The method, and why:

  1. A low-poly quad CAGE is generated from the measurements, with its topology designed rather
     than found: one tube for torso, neck and head with a quad grid on the crown, a 12-sided tube
     per arm let into a 2x4 hole in the torso's side, a hand with a palm, four fingers and a thumb,
     and a 14-sided tube per leg split off the torso's bottom ring at the crotch, closed by a grid
     sole. Every joint a skeleton will bend (shoulder, elbow, wrist, knuckles, hip, knee, ankle)
     has rings either side of it, which is what lets skinning deform it without collapsing.
  2. The cage is subdivided once (Catmull-Clark) for a smooth surface at a game budget.
  3. Every vertex is FITTED to the silhouettes: each part (torso column, arms, legs) has a measured
     cross-section at every height, and vertices move radially onto it. Parts blend at junctions
     through per-vertex weights that the subdivision interpolates for free.
  4. Seams are marked on the cage, survive the subdivision, and the final mesh is unwrapped.
  5. The colour texture is baked by projecting the four views onto the surface, each weighted by
     whether that view could actually see the texel (a sun lamp per view direction, baked as
     direct light) so an arm in front of the torso in a side view does not paint itself on it.

Frames: Blender metres, the character faces -Y, +Z up, its left hand at +X, soles at z=0.
"""

import importlib
import math
import os
import time

import bmesh
import bpy
import numpy as np
from mathutils import Vector

import sheet_measure as sm

PARTS = ("torso", "arm_l", "arm_r", "leg_l", "leg_r", "hand_l", "hand_r")
FIT_PARTS = ("torso", "arm_l", "arm_r", "leg_l", "leg_r")
TORSO_N = 24        # vertices around the torso, neck and head in the cage
ARM_N = 12          # around each arm; a 2x4 hole in the torso's side has 12 boundary vertices
LEG_N = 14          # around each leg: half the torso's bottom ring (13) plus the crotch vertex


def log(msg):
    print("[character] " + msg, flush=True)


# ==================================================================================================
# Loading
# ==================================================================================================

def load_rgb(path):
    """An image file as (H, W, 3) uint8, top row first, plus the Blender image datablock."""
    img = bpy.data.images.load(path, check_existing=True)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    rgb = (np.clip(px.reshape(h, w, 4)[::-1, :, :3], 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    return rgb, img


# ==================================================================================================
# Measured body: cross-sections as functions of height
# ==================================================================================================

def _smooth(values, window):
    """Moving median then moving mean over a 1-D array that may hold NaN gaps."""
    v = np.array(values, dtype=float)
    idx = np.arange(len(v))
    good = ~np.isnan(v)
    if good.sum() < 2:
        return v
    v = np.interp(idx, idx[good], v[good])
    half = window // 2
    pad = np.pad(v, half, mode="edge")
    med = np.array([np.median(pad[i:i + window]) for i in range(len(v))])
    pad = np.pad(med, half, mode="edge")
    return np.convolve(pad, np.ones(window) / window, mode="valid")


class Body:
    """Everything the cage and the fit need, measured once.

    Extents are averaged across the opposite views (front with back, left with right), which are
    mirror images of each other in a consistent turnaround; where they disagree the average is
    the honest answer.
    """

    STEP = 0.002

    def __init__(self, views, lm, H):
        self.views, self.lm, self.H = views, lm, H
        self.zs = np.arange(0.0, H + 1e-9, self.STEP)
        self._torso()
        self._legs()
        self._arms()
        self._collar_and_head_offsets()

    # ---- torso, neck and head ---------------------------------------------------------------
    def _torso(self):
        lm, zs = self.lm, self.zs
        xs = {"lo": [], "hi": []}
        ys = {"lo": [], "hi": []}
        for z in zs:
            ex = [self.views[n].extent_at(z, 0.0) for n in ("front", "back")]
            ex = [e for e in ex if e]
            ey = [self.views[n].extent_at(z, 0.0) for n in ("left", "right")]
            ey = [e for e in ey if e]
            if ex and z >= lm["crotch"]:
                half = np.mean([0.5 * (e[1] - e[0]) for e in ex])   # symmetric by construction
                xs["lo"].append(-half)
                xs["hi"].append(half)
            else:
                xs["lo"].append(np.nan)
                xs["hi"].append(np.nan)
            if ey and z >= lm["crotch"] - 0.02:
                ys["lo"].append(np.mean([e[0] for e in ey]))
                ys["hi"].append(np.mean([e[1] for e in ey]))
            else:
                ys["lo"].append(np.nan)
                ys["hi"].append(np.nan)
        x_hi = np.array(xs["hi"])
        # Between the armpit and the top of the shoulder the run through the axis takes in both
        # arms. The torso there is the chest, which carries on from just below the armpit.
        a, t = lm["armpit"], lm["shoulder_top"]
        chest = np.interp(a - 0.012, zs, _smooth(x_hi, 5))
        band = (zs >= a - 0.012) & (zs <= t)
        x_hi[band] = chest * (1.0 + 0.04 * (zs[band] - a) / max(t - a, 1e-6))
        y_lo = np.array(ys["lo"])
        # A hand hangs in front of the hips in the side views: its fingers stand proud of the
        # thigh's front edge. Across the band the hand occupies, the front edge is interpolated.
        h0 = min(lm["arm_l"]["tip_z"], lm["arm_r"]["tip_z"]) - 0.012
        h1 = max(lm["arm_l"]["wrist_z"], lm["arm_r"]["wrist_z"]) + 0.04
        y_lo[(zs > h0) & (zs < h1)] = np.nan
        # Above the neck the outline is hair and features: tufts, the nose, the chin. Stacked rings
        # each fitted to that read as ridges, so the head is smoothed over ~3 cm, the body over ~1.4.
        head = zs > lm["neck"] + 0.01
        self.torso_rx = np.where(head, _smooth(x_hi, 15), _smooth(x_hi, 7))
        self.torso_ylo = np.where(head, _smooth(y_lo, 15), _smooth(y_lo, 7))
        yhi_raw = np.array(ys["hi"])
        self.torso_yhi = np.where(head, _smooth(yhi_raw, 15), _smooth(yhi_raw, 7))
        # The neck. At the height where the front view is narrowest, the side view's front edge is
        # already the underside of the chin, which would make the neck a slab as deep as the jaw.
        # Across the neck itself the front comes from the nape and a near-round section instead,
        # and the chin begins at the first head ring above it.
        # The band stops short of the narrowest height itself: that height is the jaw's in the
        # front view, and it is the bottom of the chin in the side views. A band reaching it cut
        # the chin's lowest centimetre off (3.6 cm short at the neck height), and the receding chin
        # made the mouth read as a muzzle.
        n = lm["neck"]
        band = (zs >= n - 0.025) & (zs <= n - 0.006)
        round_front = self.torso_yhi - 2.0 * 1.08 * self.torso_rx
        self.torso_ylo[band] = np.maximum(self.torso_ylo[band], round_front[band])

    def _collar_and_head_offsets(self):
        """Two things about the head the texture needs.

        The collar: the lowest height where skin shows at the front of the neck. Above it is
        skin; below it is suit.

        Each side sheet's head offset. The two side sheets were generated separately and do not
        agree on where the head sits over the hips: the right sheet's head is about 3 cm further
        back than the left sheet's. The geometry takes the average; each sheet's offset from that
        average is what its projection must be shifted by, or the ears land 3 cm apart."""
        lm, H = self.lm, self.H
        f = self.views["front"]
        c0, c1 = sorted((int(f.col(-0.04)), int(f.col(0.04))))
        rows = np.where(f.skin[:int(f.row(lm["shoulder_top"])), c0:c1].any(axis=1))[0]
        self.collar_z = float(f.z(rows.max())) if len(rows) else lm["neck"] - 0.02
        centres = {}
        for n in ("left", "right"):
            v = self.views[n]
            cs = []
            for z in np.arange(lm["neck"] + 0.04, H - 0.03, 0.004):
                e = v.extent_at(z, 0.0)
                if e:
                    cs.append(0.5 * (e[0] + e[1]))
            centres[n] = float(np.median(cs)) if cs else 0.0
        mean = 0.5 * (centres["left"] + centres["right"])
        self.head_offset = {n: centres[n] - mean for n in centres}
        self.head_offset.update({"front": 0.0, "back": 0.0})

    def _head_tops(self):
        """The head's top outline: for each column over the head, the highest figure pixel, in the
        front/back views as a function of x and in the side views as a function of y. Smoothed,
        because hair tufts make the raw outline jagged."""
        lm = self.lm
        out = {}
        for axis, names in (("x", ("front", "back")), ("y", ("left", "right"))):
            hs = np.arange(-0.16, 0.16, 0.002)
            vals = []
            for h in hs:
                tops = []
                for n in names:
                    v = self.views[n]
                    c = int(round(v.col(h)))
                    if not (0 <= c < v.w):
                        continue
                    col = v.mask[:int(v.row(lm["neck"])), c]
                    rows = np.where(col)[0]
                    if len(rows):
                        tops.append(v.z(rows[0]))
                vals.append(np.mean(tops) if tops else np.nan)
            out[axis] = (hs, _smooth(np.array(vals), 9))
        self._tops = out

    def head_top(self, x, y):
        """Height of the head's top outline over (x, y): the lower of the two views' outlines."""
        if not hasattr(self, "_tops"):
            self._head_tops()
        hx, vx = self._tops["x"]
        hy, vy = self._tops["y"]
        return float(min(np.interp(x, hx, vx), np.interp(y, hy, vy)))

    def torso_section(self, z):
        """(cx, cy, rx, ry) of the torso/neck/head cross-section at height z."""
        rx = float(np.interp(z, self.zs, self.torso_rx))
        lo = float(np.interp(z, self.zs, self.torso_ylo))
        hi = float(np.interp(z, self.zs, self.torso_yhi))
        return 0.0, 0.5 * (lo + hi), max(rx, 0.004), max(0.5 * (hi - lo), 0.004)

    # ---- legs -------------------------------------------------------------------------------
    def _legs(self):
        lm, zs = self.lm, self.zs
        # The left leg (+X): its run in the front view, mirrored from the back view's right leg.
        samples = {}
        for name, side in (("front", 1.0), ("back", 1.0)):
            v = self.views[name]
            track = 0.10 * self.H / 1.8
            out = []
            for z in zs:
                if z > lm["crotch"] - 0.004:
                    out.append((np.nan, np.nan))
                    continue
                best = None
                for a, b in v.runs_at(z):
                    h0, h1 = sorted((v.h_of(a - 0.5), v.h_of(b - 0.5)))
                    mid = 0.5 * (h0 + h1)
                    if side * mid > 0 and (best is None or abs(mid - track) < abs(best[2] - track)):
                        best = (h0, h1, mid)
                if best is None:
                    out.append((np.nan, np.nan))
                    continue
                track = best[2]
                out.append((best[0], best[1]))
            samples[name] = np.array(out)
        stack = np.stack([samples["front"], samples["back"]])
        counts = (~np.isnan(stack)).sum(axis=0)
        both = np.where(counts > 0, np.nansum(stack, axis=0) / np.maximum(counts, 1), np.nan)
        self.leg_xlo = _smooth(both[:, 0], 5)
        self.leg_xhi = _smooth(both[:, 1], 5)
        # Depth: both legs overlap in the side views, so the side run is one leg's depth. Tracked
        # from the torso's centre downward, and averaged across the two sides.
        ylo, yhi = [], []
        track = {"left": None, "right": None}
        for z in zs[::-1]:
            lo_vals, hi_vals = [], []
            for n in ("left", "right"):
                v = self.views[n]
                ref = track[n] if track[n] is not None else 0.0
                best = None
                for a, b in v.runs_at(z):
                    h0, h1 = sorted((v.h_of(a - 0.5), v.h_of(b - 0.5)))
                    if best is None or abs(0.5 * (h0 + h1) - ref) < abs(0.5 * sum(best) - ref):
                        best = (h0, h1)
                if best:
                    track[n] = 0.5 * (best[0] + best[1])
                    lo_vals.append(best[0])
                    hi_vals.append(best[1])
            ylo.append(np.mean(lo_vals) if lo_vals else np.nan)
            yhi.append(np.mean(hi_vals) if hi_vals else np.nan)
        ylo = np.array(ylo[::-1])
        yhi = np.array(yhi[::-1])
        h0 = min(lm["arm_l"]["tip_z"], lm["arm_r"]["tip_z"]) - 0.012
        h1 = max(lm["arm_l"]["wrist_z"], lm["arm_r"]["wrist_z"]) + 0.04
        ylo[(zs > h0) & (zs < h1)] = np.nan
        self.leg_ylo = _smooth(ylo, 5)
        self.leg_yhi = _smooth(yhi, 5)
        # The ankle: where the side view's front edge leaves the boot shaft for the foot -- the
        # highest height below a sixth of the body where the front comes forward by 2 cm.
        shaft = float(np.interp(0.20 * self.H / 1.8, zs, self.leg_ylo))
        self.ankle_z = 0.155 * self.H / 1.8
        for z in np.arange(0.20 * self.H / 1.8, 0.05, -self.STEP):
            if float(np.interp(z, zs, self.leg_ylo)) < shaft - 0.02:
                self.ankle_z = float(z) + 0.01
                break

    def leg_section(self, z, side=1.0):
        """(cx, cy, rx, ry) of a leg at height z; side +1 left (+X), -1 right.

        Below the ankle the sheets' feet splay outward (about 12 degrees), so the front view's
        width there is part foot length. The model's feet point straight ahead, which is what a
        rig wants: the foot's width carries on from the boot shaft, flaring a little to the sole,
        and its length comes from the side view."""
        z = max(z, 0.012)
        ankle = self.ankle_z
        if z < ankle:
            lo = float(np.interp(ankle, self.zs, self.leg_xlo))
            hi = float(np.interp(ankle, self.zs, self.leg_xhi))
            flare = 1.0 + 0.18 * (ankle - z) / ankle
            mid, half = 0.5 * (lo + hi), 0.5 * (hi - lo) * flare
            lo, hi = mid - half, mid + half
        else:
            lo = float(np.interp(z, self.zs, self.leg_xlo))
            hi = float(np.interp(z, self.zs, self.leg_xhi))
        ylo = float(np.interp(z, self.zs, self.leg_ylo))
        yhi = float(np.interp(z, self.zs, self.leg_yhi))
        return side * 0.5 * (lo + hi), 0.5 * (ylo + yhi), max(0.5 * (hi - lo), 0.004), \
            max(0.5 * (yhi - ylo), 0.004)

    # ---- arms -------------------------------------------------------------------------------
    def _arms(self):
        """The left arm's axis in 3D and its cross-section along it. The right arm mirrors it."""
        lm, H = self.lm, self.H
        a, t = lm["armpit"], lm["shoulder_top"]
        # Average the two arms' fitted lines, mirrored, for a symmetric model.
        kl, cl = lm["arm_l"]["k"], lm["arm_l"]["c"]
        kr, cr = lm["arm_r"]["k"], lm["arm_r"]["c"]
        k, c = 0.5 * (kl - kr), 0.5 * (cl - cr)          # left-arm line: x = k z + c
        self.arm_k, self.arm_c = k, c
        zs_ = a + 0.6 * (t - a)                          # the shoulder joint, inside the deltoid
        zw = 0.5 * (lm["arm_l"]["wrist_z"] + lm["arm_r"]["wrist_z"])
        # Depth of the shoulder: the middle of the torso's depth at the joint's height. Depth of
        # the wrist: the top of the hand's skin in the side views.
        _, ys_, _, _ = self.torso_section(zs_)
        yw = self._side_skin_y(zw)
        self.S = Vector((k * zs_ + c, ys_, zs_))
        self.W = Vector((k * zw + c, yw, zw))
        d = self.W - self.S
        self.arm_len = d.length
        self.arm_dir = d.normalized()
        # Horizontal-run widths along the arm, per height, both arms mirrored together.
        hs, outs, ins = [], [], []
        for side, key in ((1.0, "arm_l"), (-1.0, "arm_r")):
            for z, mid, h0, h1 in lm[key]["samples"]:
                if z < zw - 0.005:
                    continue
                axis_x = side * (k * z + c)
                if side > 0:
                    outer, inner = h1 - axis_x, axis_x - h0
                else:
                    outer, inner = axis_x - h0, h1 - axis_x
                hs.append(z)
                outs.append(outer)
                ins.append(inner)
        # Above the armpit the arm is part of the run through the axis; its outer end is still
        # the deltoid's outline, so that is measured too (the inner side is taken as a mirror).
        for z in np.arange(a, t + 0.02, self.STEP):
            for n in ("front", "back"):
                e = self.views[n].extent_at(z, 0.0)
                if not e:
                    continue
                for side, edge in ((1.0, e[1]), (-1.0, -e[0])):
                    axis_x = k * z + c
                    hs.append(z)
                    outs.append(edge - axis_x)
                    ins.append(np.nan)
        order = np.argsort(hs)
        self.arm_z = np.array(hs)[order]
        cosang = abs(self.arm_dir.z) / math.hypot(self.arm_dir.x, self.arm_dir.z)
        self.arm_cos = cosang
        self.arm_out = _smooth(np.array(outs)[order] * cosang, 9)
        self.arm_in = _smooth(np.array(ins)[order] * cosang, 9)
        self.palm_half_width = self._hand_breadth() * 0.5
        # The hand's tip, for the hand's direction and length.
        tz = 0.5 * (lm["arm_l"]["tip_z"] + lm["arm_r"]["tip_z"])
        tip_x = self._front_skin_tip_x()
        self.T = Vector((tip_x, self._side_skin_tip_y(), max(tz, zw - 0.30)))

    def _hand_breadth(self):
        """Breadth of the hand across the back: in the side views the back of the hand faces the
        camera, so the span of skin across a row on the back of the hand is its breadth. The span,
        first skin pixel to last, not the widest run: shadow between the fingers splits a row into
        several runs, and the widest of those is a finger or two (that read 7 cm for a 9 cm hand)."""
        out = []
        zw = 0.5 * (self.lm["arm_l"]["wrist_z"] + self.lm["arm_r"]["wrist_z"])
        for n in ("left", "right"):
            v = self.views[n]
            widths = []
            for z in np.arange(zw - 0.08, zw - 0.025, 0.004):
                cols = np.where(v.skin[int(round(v.row(z)))])[0]
                if len(cols):
                    widths.append((cols.max() - cols.min()) * v.scale)
            if widths:
                out.append(np.median(widths))
        return float(np.mean(out)) if out else 0.085

    def _side_skin_y(self, z):
        """Mean y of skin pixels in the side views just below height z (the top of the hand)."""
        ys = []
        for n in ("left", "right"):
            v = self.views[n]
            r0 = int(round(v.row(z)))
            rows = v.skin[r0:r0 + int(0.03 / v.scale)]
            cols = np.where(rows.any(axis=0))[0]
            if len(cols):
                ys.append(np.mean([v.h_of(cc) for cc in cols]))
        return float(np.mean(ys)) if ys else 0.0

    def _side_skin_tip_y(self):
        ys = []
        for n in ("left", "right"):
            v = self.views[n]
            hand_rows = np.where(v.skin[int(v.row(self.H * 0.62)):, :].any(axis=1))[0]
            if len(hand_rows):
                r = int(v.row(self.H * 0.62)) + hand_rows.max()
                cols = np.where(v.skin[r])[0]
                ys.append(np.mean([v.h_of(cc) for cc in cols]))
        return float(np.mean(ys)) if ys else 0.0

    def _front_skin_tip_x(self):
        """x of the lowest skin pixel on the left side of the front view (mirrored from both)."""
        xs = []
        for n, side in (("front", 1.0), ("back", -1.0)):
            v = self.views[n]
            sk = v.skin.copy()
            sk[:int(v.row(self.H * 0.62)), :] = False        # below the face
            rows = np.where(sk.any(axis=1))[0]
            if not len(rows):
                continue
            for half in (1.0, -1.0):
                cols_all = np.arange(v.w)
                hx = np.array([v.h_of(cc) for cc in cols_all])
                mask = sk & ((hx * half) > 0)[None, :]
                rr = np.where(mask.any(axis=1))[0]
                if len(rr):
                    r = rr.max()
                    cc = np.where(mask[r])[0]
                    xs.append(abs(np.mean(hx[cc])))
        return float(np.mean(xs)) if xs else self.W.x

    def arm_profile(self, t):
        """(outer, inner) half-widths perpendicular to the arm axis, at parameter t (0 shoulder
        joint, 1 wrist), from the horizontal runs. Above the armpit the run takes in the torso
        too, so only the outer edge is trusted there and the inner mirrors it."""
        p = self.S + self.arm_dir * (t * self.arm_len)
        z = p.z
        outer = float(np.interp(z, self.arm_z, self.arm_out))
        inner = float(np.interp(z, self.arm_z, self.arm_in))
        if z > self.lm["armpit"] - 0.01 or not (0.0 < inner < 1.6 * outer):
            inner = outer
        return max(outer, 0.01), max(inner, 0.01)


# ==================================================================================================
# The cage
# ==================================================================================================

def _smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def superellipse_point(rx, ry, phi, n):
    """Point on a superellipse at parameter phi, measured from the front (-Y) toward +X."""
    s, c = math.sin(phi), math.cos(phi)
    x = rx * math.copysign(abs(s) ** (2.0 / n), s)
    y = -ry * math.copysign(abs(c) ** (2.0 / n), c)
    return x, y


def superellipse_radius(rx, ry, phi, n):
    """Distance from the centre to the superellipse in direction phi (radial form)."""
    s, c = abs(math.sin(phi)), abs(math.cos(phi))
    return ((s / rx) ** n + (c / ry) ** n) ** (-1.0 / n)


class Cage:
    def __init__(self):
        self.verts = []
        self.weights = []           # per vertex: dict part -> weight
        self.faces = []
        self.seams = set()
        self.marks = {}             # named vertex groups of interest (rings etc.)

    def v(self, co, part=None, w=1.0):
        self.verts.append(Vector(co))
        self.weights.append({part: w} if part else {})
        return len(self.verts) - 1

    def face(self, *idx):
        self.faces.append(tuple(idx))

    def seam(self, a, b):
        self.seams.add((min(a, b), max(a, b)))

    def seam_loop(self, ring, closed=True):
        n = len(ring)
        for i in range(n if closed else n - 1):
            self.seam(ring[i], ring[(i + 1) % n])

    def bridge(self, r0, r1):
        """Quads between two rings of equal length, in order."""
        n = len(r0)
        for i in range(n):
            j = (i + 1) % n
            self.face(r0[i], r0[j], r1[j], r1[i])


def grid_patch(cage, ring, m, n, part, lift=0.0, flat_z=None, start=None, weight=1.0):
    """Close a ring of 2(m+n) vertices with an m x n quad grid (all quads, no pole).

    The ring is laid around the grid's perimeter starting at `start` (a perimeter index; by
    default the middle of the first side, so a ring that starts at its front centre puts the
    front centre mid-side). Interior points are a Coons patch of the boundary, lifted by `lift`
    toward the middle (a dome) or flattened to `flat_z`.
    """
    assert len(ring) == 2 * (m + n)
    perim = [(0, b) for b in range(n)] + [(a, n) for a in range(m)] + \
            [(m, b) for b in range(n, 0, -1)] + [(a, 0) for a in range(m, 0, -1)]
    if start is None:
        start = n // 2
    grid = {}
    for i, vi in enumerate(ring):
        grid[perim[(start + i) % len(perim)]] = vi
    P = lambda a, b: cage.verts[grid[(a, b)]]

    def bnd(a, b):
        return P(a, b)

    for a in range(1, m):
        for b in range(1, n):
            u, v = a / m, b / n
            p = ((1 - u) * bnd(0, b) + u * bnd(m, b) + (1 - v) * bnd(a, 0) + v * bnd(a, n)
                 - ((1 - u) * (1 - v) * bnd(0, 0) + u * (1 - v) * bnd(m, 0)
                    + (1 - u) * v * bnd(0, n) + u * v * bnd(m, n)))
            if flat_z is not None:
                p.z = flat_z
            else:
                p.z += lift * (1 - (2 * u - 1) ** 2) * (1 - (2 * v - 1) ** 2)
            grid[(a, b)] = cage.v(p, part, weight)
    for a in range(m):
        for b in range(n):
            cage.face(grid[(a, b)], grid[(a, b + 1)], grid[(a + 1, b + 1)], grid[(a + 1, b)])
    return grid


def _unwrap(angles):
    out = [angles[0]]
    for a in angles[1:]:
        while a < out[-1]:
            a += 2 * math.pi
        out.append(a)
    return out


def build_cage(body, cfg):
    """The low-poly cage, symmetric about x=0, with part weights and seams."""
    lm, H = body.lm, body.H
    C, A, T, N = lm["crotch"], lm["armpit"], lm["shoulder_top"], lm["neck"]
    cage = Cage()
    nT = TORSO_N
    phis = [2 * math.pi * k / nT for k in range(nT)]

    # ---- torso, neck and head rings --------------------------------------------------------
    ring_z = ([C + f * (A - C) for f in (0.07, 0.21, 0.38, 0.56, 0.73, 0.88)]
              + [A - 0.004, A + 0.5 * (T - A), T]
              + [T + 0.55 * (N - T), T + 0.82 * (N - T), N]
              + [N + f * (H - N) for f in (0.11, 0.22, 0.33, 0.44, 0.555, 0.665, 0.77, 0.875)])
    i_hole = 6                               # rings 6..8 border the arm holes
    rings = []
    for i, z in enumerate(ring_z):
        cx, cy, rx, ry = body.torso_section(z)
        n_exp = cfg["torso_n"] if z < N else cfg["head_n"]
        ring = []
        for k, phi in enumerate(phis):
            x, y = superellipse_point(rx, ry, phi, n_exp)
            ring.append(cage.v((cx + x, cy + y, z), "torso"))
        rings.append(ring)
    cage.marks["torso_rings"] = rings
    cage.marks["ring_z"] = ring_z
    # faces, leaving the 2x4 arm holes open on both sides
    hole_cols = {"l": [4, 5, 6, 7], "r": [16, 17, 18, 19]}
    skip = {(i, k) for i in (i_hole, i_hole + 1) for side in hole_cols for k in hole_cols[side]}
    for i in range(len(rings) - 1):
        for k in range(nT):
            if (i, k) in skip:
                continue
            kk = (k + 1) % nT
            cage.face(rings[i][k], rings[i][kk], rings[i + 1][kk], rings[i + 1][k])
    # crown: the top ring closed by a 6x6 grid
    top = rings[-1]
    grid_patch(cage, top, 6, 6, "torso", lift=(H - ring_z[-1]) * 1.05)
    # seams: back of the torso and head, the neck (collar top) ring
    i_collar = ring_z.index(T + 0.82 * (N - T))
    for i in range(len(rings) - 1):
        cage.seam(rings[i][12], rings[i + 1][12])
    cage.seam_loop(rings[i_collar])

    # ---- arms ------------------------------------------------------------------------------
    arm_rings = {}
    for side, sgn in (("l", 1.0), ("r", -1.0)):
        cols = hole_cols[side] + [hole_cols[side][-1] + 1]          # 5 columns of vertices
        r6, r7, r8 = rings[i_hole], rings[i_hole + 1], rings[i_hole + 2]
        hole = [r6[k] for k in cols] + [r7[cols[-1]]] + [r8[k] for k in reversed(cols)] + [r7[cols[0]]]
        for vi in hole:
            cage.weights[vi] = {"arm_" + side: 1.0}
        arm_rings[side] = build_arm(cage, body, cfg, hole, sgn, side)
        cage.seam_loop(hole)

    # ---- legs ------------------------------------------------------------------------------
    r0 = rings[0]
    _, cy_c, _, _ = body.torso_section(C)
    crotch = cage.v((0.0, cy_c, C), None)
    cage.marks["crotch"] = crotch
    for side, sgn in (("l", 1.0), ("r", -1.0)):
        if sgn > 0:
            loop0 = [r0[k] for k in range(0, 13)] + [crotch]
        else:
            loop0 = [r0[k % nT] for k in range(24, 11, -1)] + [crotch]
        build_leg(cage, body, cfg, loop0, sgn, side)
        cage.seam_loop(loop0)
    return cage


def _arm_frame(d, sgn):
    """Outward-up perpendicular in the front plane, and the depth axis, for an arm direction."""
    Y = Vector((0.0, 1.0, 0.0))
    u = d.cross(Y)
    if sgn < 0:
        u = -u
    u.normalize()
    v = (Y - d * Y.dot(d) - u * Y.dot(u)).normalized()
    return u, v


def _forearm_roll(u, v, t, cfg):
    """The arm's cross-section frame at parameter t, rolled about the arm toward the hand's roll.
    Forearms pronate along their length, so the turn ramps in from the elbow to the wrist rather
    than twisting the wrist in one segment."""
    beta = math.radians(cfg.get("hand_roll", 35.0)) * _smoothstep(0.55, 1.0, t)
    return (u * math.cos(beta) - v * math.sin(beta)), (u * math.sin(beta) + v * math.cos(beta))


def _ring_angles(points, centre, u, v):
    """Angle of each point around an axis, from the front (-v) toward u."""
    out = []
    for p in points:
        r = p - centre
        out.append(math.atan2(r.dot(u), -r.dot(v)))
    return out


def build_arm(cage, body, cfg, hole, sgn, side):
    """An arm from its torso hole to the wrist, then the hand. Returns the wrist ring."""
    part = "arm_" + side
    S = body.S.copy()
    W = body.W.copy()
    S.x *= sgn
    W.x *= sgn
    d = (W - S).normalized()
    L = (W - S).length
    u, v = _arm_frame(d, sgn)
    centre0 = sum((cage.verts[i] for i in hole), Vector()) / len(hole)
    ang0 = _ring_angles([cage.verts[i] for i in hole], centre0, u, v)
    # Order the hole by angle, starting at the vertex nearest the front, so ring vertex j matches
    # hole vertex j and the first segment does not twist.
    wrapped = [(a + 2 * math.pi) % (2 * math.pi) for a in ang0]
    order = sorted(range(len(hole)), key=lambda i: wrapped[i])
    first = min(range(len(order)), key=lambda q: min(wrapped[order[q]], 2 * math.pi - wrapped[order[q]]))
    order = order[first:] + order[:first]
    hole = [hole[i] for i in order]
    ang0 = [wrapped[i] for i in order]
    if ang0[0] > math.pi:
        ang0[0] -= 2 * math.pi
    ang0 = _unwrap(ang0)
    # The uniform set puts vertex 3 on the outside (u) and 9 on the inside: the hand needs a
    # vertex on the back of the hand and one on the palm. Shifted by whole turns to sit nearest
    # the hole's angles, so blending the two never goes the long way round.
    uniform = [2 * math.pi * j / ARM_N for j in range(ARM_N)]
    turns = round((ang0[0] - uniform[0]) / (2 * math.pi))
    uniform = [a + 2 * math.pi * turns for a in uniform]
    ts = [0.10, 0.24, 0.38, 0.48, 0.55, 0.62, 0.76, 0.90, 1.00]
    blends = [0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
    prev = hole
    rings = [hole]
    for t, blend in zip(ts, blends):
        c = S + d * (t * L)
        outer, inner = body.arm_profile(t)
        ru = 0.5 * (outer + inner)
        off = 0.5 * (outer - inner)
        depth_ratio = np.interp(t, [0.0, 0.5, 0.75, 1.0], cfg["arm_depth_ratio"])
        rv = ru * depth_ratio
        cc = c + u * off
        ub, vb = _forearm_roll(u, v, t, cfg)
        ring = []
        for j in range(ARM_N):
            a = (1 - blend) * ang0[j] + blend * uniform[j]
            x, y = superellipse_point(ru, rv, a, 2.0)
            ring.append(cage.v(cc + ub * x + vb * (y), part))   # y is -rv cos: front is -v
        cage.bridge(prev, ring)
        rings.append(ring)
        prev = ring
    # underside seam (vertex 9: the inside of the arm, facing the body)
    for a, b in zip(rings[:-1], rings[1:]):
        cage.seam(a[9 % len(a)] if a is not hole else a[9], b[9])
    cage.seam_loop(prev)                       # the wrist
    build_hand(cage, body, cfg, prev, S, W, d, u, v, sgn, side)
    return prev


def build_hand(cage, body, cfg, wrist, S, W, d_arm, u_arm, v_arm, sgn, side):
    """Palm (two rings), four fingers off a knuckle ring, and a thumb out of the palm's side."""
    part = "hand_" + side
    T = body.T.copy()
    T.x *= sgn
    d = (T - W).normalized()
    Lh = (T - W).length
    u = (u_arm - d * u_arm.dot(d)).normalized()          # back of the hand
    v = d.cross(u) if sgn > 0 else u.cross(d)             # toward the pinky (+Y-ish)
    if v.y < 0:
        v = -v
    v.normalize()
    # Relaxed hands do not face the thighs squarely: the palm turns toward the back, the thumb
    # comes forward and the back of the hand faces out and a little forward. Both sheets show it:
    # the front view sees more of the hand than its edge, the side view less than its breadth.
    roll = math.radians(cfg.get("hand_roll", 35.0))
    u, v = (u * math.cos(roll) - v * math.sin(roll)).normalized(), \
        (u * math.sin(roll) + v * math.cos(roll)).normalized()
    # The side views' skin span includes the thumb where it stands forward of the hand, so the
    # measured breadth is bounded by the hand's length: a hand is about 0.45 as broad as it is long.
    hw = getattr(body, "palm_half_width", cfg["palm_half_width"])
    hw = min(max(hw, 0.20 * Lh), 0.225 * Lh)
    th = cfg["palm_half_thickness"]
    P = cfg["palm_fraction"] * Lh
    # Ring vertex j: 0 thumb edge, 1..5 back of the hand (thumb to pinky), 6 pinky edge,
    # 7..11 palm (pinky to thumb). The fingers need the back and palm vertices evenly spread.
    spread = [-0.85, -0.42, 0.0, 0.42, 0.85]

    def palm_ring(centre, hw_, th_):
        pts = [centre - v * hw_]
        pts += [centre + u * th_ + v * (s * hw_) for s in spread]
        pts += [centre + v * hw_]
        pts += [centre - u * th_ + v * (s * hw_) for s in reversed(spread)]
        return [cage.v(p, part) for p in pts]

    p1 = palm_ring(W + d * (0.45 * P), hw * 0.92, th * 1.05)
    p2 = palm_ring(W + d * P, hw, th * 0.85)
    cage.bridge(wrist, p1)
    # p1 -> p2, leaving the thumb's hole: the palm-side face next to the thumb edge, halfway down
    # the palm, which is where a thumb leaves the hand (at the wrist it reads as a stick).
    n = len(p1)
    for j in range(n):
        jj = (j + 1) % n
        if j == 11:
            continue
        cage.face(p1[j], p1[jj], p2[jj], p2[j])
    thumb_hole = [p1[11], p1[0], p2[0], p2[11]]
    # knuckles: the side vertices close with a triangle each, the rest belong to fingers
    cage.face(p2[1], p2[0], p2[11])
    cage.face(p2[5], p2[7], p2[6])
    lengths = cfg["finger_lengths"]
    for i in range(4):
        base = [p2[1 + i], p2[2 + i], p2[10 - i], p2[11 - i]]
        build_finger(cage, base, d, u, v, lengths[i] * Lh, cfg, part)
    build_finger(cage, thumb_hole, (0.82 * d - 0.48 * v - 0.30 * u).normalized(), u, v,
                 cfg["thumb_length"] * Lh, cfg, part, thumb=True)
    # seams: palm split along the pinky edge
    cage.seam(wrist[6], p1[6])
    cage.seam(p1[6], p2[6])


def build_finger(cage, base, d, u, v, length, cfg, part, thumb=False):
    """A finger as 4-vertex rings with a gentle curl toward the palm, capped with one quad."""
    centre = sum((cage.verts[i] for i in base), Vector()) / 4.0
    pts = [cage.verts[i] - centre for i in base]
    # half-sizes from the base loop, tapering toward the tip
    half_u = max(abs(p.dot(u)) for p in pts)
    half_v = max(abs(p.dot(v)) for p in pts)
    if thumb:
        half_u = half_v = cfg["thumb_radius"]
    stations = [0.14, 0.42, 0.70, 0.92]
    curl = [math.radians(a) for a in cfg["finger_curl"]]
    dirn = d.copy()
    pos = centre.copy()
    prev_s = 0.0
    prev = base
    rings = [base]
    for idx, s in enumerate(stations):
        # bend toward the palm (-u) about v at each joint
        ang = curl[min(idx, len(curl) - 1)]
        dirn = (dirn * math.cos(ang) - u * math.sin(ang)).normalized()
        pos = pos + dirn * ((s - prev_s) * length)
        prev_s = s
        taper = 1.0 - 0.28 * s
        uu = (u - dirn * u.dot(dirn)).normalized()
        vv = dirn.cross(uu)
        if vv.dot(v) < 0:
            vv = -vv
        hu = half_u * taper * (0.85 if not thumb else 1.0)
        hv = half_v * taper * 0.92
        # same corner order as the base loop: (+u -v), (+u +v), (-u +v), (-u -v) -- or, for the
        # thumb hole, whatever order its corners came in, matched by angle.
        if thumb:
            angs = [math.atan2(p.dot(uu), -p.dot(vv)) for p in pts]
            ring = []
            for a in angs:
                ring.append(cage.v(pos + uu * (hu * math.sin(a) * 1.25) - vv * (hv * math.cos(a) * 1.25), part))
        else:
            ring = [cage.v(pos + uu * hu - vv * hv, part), cage.v(pos + uu * hu + vv * hv, part),
                    cage.v(pos - uu * hu + vv * hv, part), cage.v(pos - uu * hu - vv * hv, part)]
        cage.bridge(prev, ring)
        rings.append(ring)
        prev = ring
    cage.face(prev[3], prev[2], prev[1], prev[0])
    cage.seam_loop(base)
    for a, b in zip(rings[:-1], rings[1:]):
        cage.seam(a[3], b[3])


def build_leg(cage, body, cfg, loop0, sgn, side):
    """A leg from its loop at the crotch to a flat grid sole."""
    part = "leg_" + side
    C = body.lm["crotch"]
    k = body.H / 1.8
    ank = body.ankle_z
    zs = [C - 0.03, C - 0.11, C - 0.19, 0.54 * k, 0.50 * k, 0.46 * k, 0.40 * k, 0.33 * k,
          0.28 * k, 0.22 * k, ank + 0.02, ank - 0.025, 0.65 * ank, 0.36 * ank, 0.14 * ank, 0.0]
    cx0, cy0, _, _ = body.leg_section(C - 0.03, sgn)
    centre0 = Vector((cx0, cy0, C))
    Y = Vector((0.0, 1.0, 0.0))
    # angles around the leg, measured from the front toward +X (outward for the left leg; the
    # right leg is built in its own mirrored frame so the same code serves both)
    U = Vector((sgn, 0.0, 0.0))
    ang0 = []
    for vi in loop0:
        r = cage.verts[vi] - centre0
        ang0.append(math.atan2(r.dot(U), -r.dot(Y)))
    ang0 = _unwrap([(a + 2 * math.pi) % (2 * math.pi) for a in ang0])
    uniform = [ang0[0] + 2 * math.pi * j / LEG_N for j in range(LEG_N)]
    prev = loop0
    rings = [loop0]
    for i, z in enumerate(zs):
        blend = 0.5 if i == 0 else 1.0
        cx, cy, rx, ry = body.leg_section(z, sgn)
        n_exp = cfg["leg_n"] if z > 0.16 * body.H / 1.8 else cfg["boot_n"]
        ring = []
        for j in range(LEG_N):
            a = (1 - blend) * ang0[j] + blend * uniform[j]
            x, y = superellipse_point(rx, ry, a, n_exp)
            p = Vector((cx + sgn * x, cy + y, z))
            w = 1.0 if z > 0.005 else 0.0
            ring.append(cage.v(p, part, w))
        cage.bridge(prev, ring)
        rings.append(ring)
        prev = ring
    # sole: 4 x 3 grid, started at the vertex nearest the front-inner corner
    centre = sum((cage.verts[i] for i in prev), Vector()) / len(prev)
    best, best_d = 0, 1e9
    for j, vi in enumerate(prev):
        r = cage.verts[vi] - centre
        dd = (r.x * sgn + 0.4) ** 2 + (r.y + 1.0) ** 2        # toward front-inner
        if dd < best_d:
            best, best_d = j, dd
    ring = prev[best:] + prev[:best]
    grid_patch(cage, ring, 4, 3, part, flat_z=0.0, start=0, weight=0.0)
    # seams: inner leg (the crotch's column), the sole's edge
    for a, b in zip(rings[:-1], rings[1:]):
        cage.seam(a[-1] if a is loop0 else a[LEG_N - 1], b[LEG_N - 1])
    cage.seam_loop(prev)


# ==================================================================================================
# Object, subdivision and fit
# ==================================================================================================

def cage_to_object(cage, name, collection):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in cage.verts], [], cage.faces)
    me.validate()
    me.update()
    obj = bpy.data.objects.new(name, me)
    collection.objects.link(obj)
    for part in PARTS:
        attr = me.attributes.new("w_" + part, "FLOAT", "POINT")
        vals = [cage.weights[i].get(part, 0.0) for i in range(len(cage.verts))]
        attr.data.foreach_set("value", vals)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    lookup = {}
    for e in bm.edges:
        a, b = e.verts[0].index, e.verts[1].index
        lookup[(min(a, b), max(a, b))] = e
    missing = 0
    for key in cage.seams:
        e = lookup.get(key)
        if e is None:
            missing += 1
        else:
            e.seam = True
    # The torso rings are made whole and the arm holes cut afterwards, which leaves each hole's
    # interior vertices (three per side) attached to nothing.
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    if missing:
        log("seams naming a non-edge: %d" % missing)
    return obj


def subdivide(obj, levels=1):
    mod = obj.modifiers.new("Subdivide", "SUBSURF")
    mod.levels = levels
    mod.render_levels = levels
    mod.uv_smooth = "PRESERVE_BOUNDARIES"
    mod.boundary_smooth = "ALL"
    with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
        bpy.ops.object.modifier_apply(modifier=mod.name)


def _arm_target(body, p, sgn, cfg):
    S = body.S.copy()
    W = body.W.copy()
    S.x *= sgn
    W.x *= sgn
    d = (W - S).normalized()
    L = (W - S).length
    t = max(0.0, min(1.0, (p - S).dot(d) / L))
    c = S + d * (t * L)
    u, v = _arm_frame(d, sgn)
    outer, inner = body.arm_profile(t)
    ru = 0.5 * (outer + inner)
    cc = c + u * (0.5 * (outer - inner))
    rv = ru * float(np.interp(t, [0.0, 0.5, 0.75, 1.0], cfg["arm_depth_ratio"]))
    u, v = _forearm_roll(u, v, t, cfg)
    r = p - cc
    r = r - d * r.dot(d)
    a = math.atan2(r.dot(u), -r.dot(v))
    rad = superellipse_radius(ru, rv, a, 2.0)
    return cc + u * (rad * math.sin(a)) - v * (rad * math.cos(a)) + d * (p - cc).dot(d)


def fit(obj, body, cfg):
    """Move every vertex onto its part's measured cross-section, blended by part weight."""
    me = obj.data
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    w = {}
    for part in FIT_PARTS:
        arr = np.empty(n)
        me.attributes["w_" + part].data.foreach_get("value", arr)
        w[part] = arr
    N = body.lm["neck"]
    C = body.lm["crotch"]
    H = body.H
    normals = np.empty(n * 3)
    me.vertices.foreach_get("normal", normals)
    normals = normals.reshape(n, 3)
    new = co.copy()
    moved = 0
    for i in range(n):
        p = Vector(co[i])
        delta = Vector()
        for part in FIT_PARTS:
            wp = w[part][i]
            if wp < 1e-3:
                continue
            if part == "torso":
                # One ellipse round both hips is wrong in the crotch's notch: a vertex on the inner
                # thigh would be thrown out to the hip's outline. The torso lets go of the surface
                # over the last few centimetres above the crotch, where the legs take it.
                wp *= _smoothstep(C + 0.005, C + 0.06, p.z)
                if wp < 1e-3:
                    continue
                cx, cy, rx, ry = body.torso_section(p.z)
                n_exp = cfg["torso_n"] if p.z < N else cfg["head_n"]
                a = math.atan2(p.x - cx, -(p.y - cy))
                rad = superellipse_radius(rx, ry, a, n_exp)
                tgt = Vector((cx + rad * math.sin(a), cy - rad * math.cos(a), p.z))
                # The crown keeps the dome the subdivision gives it, but nothing may stand above the
                # head's outline in either view. (Pulling every upward-facing vertex onto the
                # lower of the two outlines made a flat-topped box: that is the visual hull.)
                if p.z > N + 0.70 * (H - N):
                    tgt.z = min(p.z, body.head_top(p.x, p.y))
            elif part.startswith("arm"):
                tgt = _arm_target(body, p, 1.0 if part.endswith("l") else -1.0, cfg)
            else:
                sgn = 1.0 if part.endswith("l") else -1.0
                cx, cy, rx, ry = body.leg_section(p.z, sgn)
                n_exp = cfg["leg_n"] if p.z > 0.16 * body.H / 1.8 else cfg["boot_n"]
                a = math.atan2((p.x - cx) * sgn, -(p.y - cy))
                rad = superellipse_radius(rx, ry, a, n_exp)
                tgt = Vector((cx + sgn * rad * math.sin(a), cy - rad * math.cos(a), p.z))
            delta += (tgt - p) * wp
        if delta.length > 0:
            moved += 1
        new[i] = p + delta
    # Smooth the corrections, not the surface: each ring is fitted to its own height's profile,
    # and where the profile changes quickly (chin, collar, knee) neighbouring rings disagree and
    # leave a ridge. Averaging each correction with its neighbours' a few times keeps the fit and
    # loses the ridges. Hands are left alone -- they were never fitted.
    deltas = new - co
    fitted = np.zeros(n, dtype=bool)
    for part in FIT_PARTS:
        fitted |= w[part] > 1e-3
    nbrs = [[] for _ in range(n)]
    for e in me.edges:
        a, b = e.vertices
        nbrs[a].append(b)
        nbrs[b].append(a)
    for _ in range(cfg.get("fit_smooth_iterations", 3)):
        avg = np.array([deltas[nb].mean(axis=0) if nb else deltas[i] for i, nb in enumerate(nbrs)])
        deltas = np.where(fitted[:, None], 0.5 * deltas + 0.5 * avg, deltas)
    new = co + deltas
    me.vertices.foreach_set("co", new.reshape(-1))
    me.update()
    log("fit: moved %d of %d vertices" % (moved, n))


# ==================================================================================================
# Orchestration
# ==================================================================================================

def measure(sheets, H):
    views = {}
    for name in ("front", "left", "back", "right"):
        rgb, _img = load_rgb(sheets % name)
        views[name] = sm.View(name, rgb, H)
    sm.register_axes(views, H)
    lm = sm.find_landmarks(views, H)
    return views, lm


def mesh_stats(obj):
    """Counts that say whether the mesh is game-ready: closed, manifold, one piece, mostly quads."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    tris = sum(1 for f in bm.faces if len(f.verts) == 3)
    quads = sum(1 for f in bm.faces if len(f.verts) == 4)
    ngons = sum(1 for f in bm.faces if len(f.verts) > 4)
    boundary = sum(1 for e in bm.edges if e.is_boundary)
    nonmanifold = sum(1 for e in bm.edges if not e.is_manifold)
    # connected components
    seen, comps = set(), 0
    for v in bm.verts:
        if v.index in seen:
            continue
        comps += 1
        stack = [v]
        while stack:
            x = stack.pop()
            if x.index in seen:
                continue
            seen.add(x.index)
            stack.extend(e.other_vert(x) for e in x.link_edges)
    volume = bm.calc_volume(signed=True)
    seams = sum(1 for e in bm.edges if e.seam)
    lo = [min(v.co[i] for v in bm.verts) for i in range(3)]
    hi = [max(v.co[i] for v in bm.verts) for i in range(3)]
    euler = len(bm.verts) - len(bm.edges) + len(bm.faces)
    out = {"verts": len(bm.verts), "faces": len(bm.faces), "tris": tris, "quads": quads,
           "ngons": ngons, "triangles_when_triangulated": tris + 2 * quads + sum(len(f.verts) - 2 for f in bm.faces if len(f.verts) > 4),
           "boundary_edges": boundary, "nonmanifold_edges": nonmanifold, "components": comps,
           "euler": euler, "signed_volume_m3": round(volume, 5), "seam_edges": seams,
           "bounds": [[round(a, 4) for a in lo], [round(b, 4) for b in hi]]}
    bm.free()
    return out


def _collection(name):
    scene = bpy.context.scene
    old = bpy.data.collections.get(name)
    if old is not None:
        for o in list(old.all_objects):
            bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.collections.remove(old)
    coll = bpy.data.collections.new(name)
    scene.collection.children.link(coll)
    return coll


def quick_renders(obj, out_dir, tag):
    """Workbench looks: front, left side, back and three-quarter, flat and with a wire overlay."""
    scene = bpy.context.scene
    os.makedirs(out_dir, exist_ok=True)
    scene.render.engine = "BLENDER_WORKBENCH"
    s = scene.display.shading
    s.light, s.color_type = "STUDIO", "SINGLE"
    s.single_color = (0.62, 0.64, 0.70)
    s.show_cavity = True
    s.cavity_type = "BOTH"
    s.show_object_outline = False
    scene.display.render_aa = "8"
    scene.render.film_transparent = False
    scene.render.resolution_x, scene.render.resolution_y = 900, 1200
    scene.render.resolution_percentage = 100
    cam_data = bpy.data.cameras.get("CharCam") or bpy.data.cameras.new("CharCam")
    cam = bpy.data.objects.get("CharCam") or bpy.data.objects.new("CharCam", cam_data)
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    scene.camera = cam
    H = obj.dimensions.z
    shots = {"front": ((0, -6, H / 2), (math.radians(90), 0, 0)),
             "left": ((6, 0, H / 2), (math.radians(90), 0, math.radians(90))),
             "back": ((0, 6, H / 2), (math.radians(90), 0, math.radians(180))),
             "threequarter": ((3.2, -4.6, H * 0.62), None)}
    paths = []
    wire = obj.modifiers.new("_wire", "WIREFRAME")
    wire.show_viewport = wire.show_render = False
    for name, (loc, rot) in shots.items():
        cam.location = loc
        if rot is None:
            cam_data.type = "PERSP"
            cam_data.lens = 85
            dvec = Vector((0, 0, H * 0.5)) - Vector(loc)
            cam.rotation_euler = dvec.to_track_quat("-Z", "Y").to_euler()
        else:
            cam_data.type = "ORTHO"
            cam_data.ortho_scale = H * 1.12
            cam.rotation_euler = rot
        path = os.path.join(out_dir, "%s_%s.png" % (tag, name))
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        paths.append(path)
    obj.modifiers.remove(wire)
    return paths


def run(name, H, sheets, cfg, args):
    t0 = time.time()
    if bpy.app.background:
        for o in list(bpy.data.objects):
            bpy.data.objects.remove(o, do_unlink=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"
    views, lm = measure(sheets, H)
    log("measured in %.1fs: crotch %.3f armpit %.3f shoulder %.3f neck %.3f" % (
        time.time() - t0, lm["crotch"], lm["armpit"], lm["shoulder_top"], lm["neck"]))
    body = Body(views, lm, H)
    log("arm: shoulder %s wrist %s length %.3f, hand tip %s, palm half-width %.3f" % (
        tuple(round(c, 3) for c in body.S), tuple(round(c, 3) for c in body.W), body.arm_len,
        tuple(round(c, 3) for c in body.T), body.palm_half_width))
    coll = _collection(name)
    cage = build_cage(body, cfg)
    obj = cage_to_object(cage, name, coll)
    stats = mesh_stats(obj)
    log("cage: %s" % stats)
    out_dir = os.path.join(args["out"], name + "_renders")
    if args["stage"] == "cage":
        quick_renders(obj, out_dir, "cage")
        return obj
    subdivide(obj, 1)
    # Mirror partners are found now, while the subdivided cage is still exactly symmetric.
    pairs, worst = mirror_pairs(obj)
    log("mirror pairs: worst partner %.3f mm away" % (1000 * worst))
    fit(obj, body, cfg)
    symmetrize(obj, pairs)
    for _ in range(cfg.get("snap_passes", 3)):
        silhouette_snap(obj, body, views, cfg, pairs, out_dir)
    for p in obj.data.polygons:
        p.use_smooth = True
    stats = mesh_stats(obj)
    log("fitted: %s" % stats)
    iou = silhouette_report(obj, views, H, out_dir, "fit")
    log("silhouette IoU against the sheets: %s" % iou)
    if args["stage"] == "fit":
        quick_renders(obj, out_dir, "fit")
        return obj
    import character_texture as ct
    importlib.reload(ct)
    uv = ct.unwrap(obj, body, cfg)
    log("uv: %s" % uv)
    cfg = dict(cfg)
    cfg.update(region_colours(views, lm))
    tex_name = "T_%s_BaseColor.png" % name
    tex_path = os.path.join(args["engine"], tex_name)
    image, tex = ct.bake_texture(obj, body, views, cfg, cfg.get("texture_size", 2048), tex_path)
    ct.make_material(obj, "MAT_" + name, image, cfg.get("roughness", 0.62))
    log("texture written: %s" % tex_path)
    if args["stage"] == "texture":
        return obj
    import character_finish as cf
    importlib.reload(cf)
    cf.strip_build_attributes(obj)
    rows = cf.run_checks(obj, body, iou, uv, tex, cfg)
    failed = [r[0] for r in rows if not r[3]]
    if failed:
        raise SystemExit("character checks failed: " + ", ".join(failed))
    if args.get("check_only"):
        log("checks passed; --check-only, so nothing written")
        return obj
    written = cf.renders(obj, body, views, out_dir, name)
    joints = os.path.join(args["engine"], name + "_joints.json")
    cf.joints_manifest(body, joints)
    fbx = os.path.join(args["engine"], name + ".fbx")
    cf.export_fbx(obj, fbx)
    blend = os.path.join(args["out"], name + ".blend")
    cf.save_blend(blend, image)
    log("wrote %s, %s, %s, %d renders in %s" % (fbx, joints, blend, len(written), out_dir))
    log("done in %.1fs" % (time.time() - t0))
    return obj


def region_colours(views, lm):
    """Median colours of the suit, the skin and the hair, read off the front sheet -- the fallback
    for texels no sheet sees well (soles, the crown's very top, deep under the arms)."""
    f = views["front"]

    def median(rows, cols, mask):
        sel = mask[rows[0]:rows[1], cols[0]:cols[1]]
        px = f.rgb[rows[0]:rows[1], cols[0]:cols[1]][sel]
        return [float(c) for c in np.median(px, axis=0)] if len(px) else [60.0, 60.0, 90.0]

    def rows(z0, z1):
        return int(f.row(z1)), int(f.row(z0))

    def cols(h0, h1):
        c0, c1 = sorted((int(f.col(h0)), int(f.col(h1))))
        return c0, c1

    neck, H = lm["neck"], f.height
    suit = median(rows(1.0 * H / 1.8, 1.2 * H / 1.8), cols(-0.08, 0.08), f.mask & ~f.skin)
    skin = median(rows(neck + 0.03, neck + 0.12), cols(-0.04, 0.04), f.skin)
    hair = median(rows(H - 0.03, H - 0.004), cols(-0.05, 0.05), f.mask & ~f.skin)
    return {"fallback_suit": suit, "fallback_skin": skin, "fallback_hair": hair}


# ==================================================================================================
# Check: does the built mesh match the sheets?
# ==================================================================================================

def _sheet_camera(view, H):
    """Orthographic camera placement that renders the model onto this sheet's pixel grid."""
    s = view.scale
    hc = view.sign * (view.w / 2.0 - 0.5 - view.axis_col) * s     # model coord at the image centre
    zc = (view.bottom - (view.h / 2.0 - 0.5)) * s
    if view.name == "front":
        return (hc, -20.0, zc), (math.radians(90), 0.0, 0.0)
    if view.name == "back":
        return (hc, 20.0, zc), (math.radians(90), 0.0, math.radians(180))
    if view.name == "left":
        return (20.0, hc, zc), (math.radians(90), 0.0, math.radians(90))
    return (-20.0, hc, zc), (math.radians(90), 0.0, math.radians(-90))


def render_silhouettes(obj, views, H, tmp_dir):
    """Render the mesh as a flat white shape from each sheet's camera, at the sheet's resolution.
    Returns {view: bool mask}."""
    scene = bpy.context.scene
    os.makedirs(tmp_dir, exist_ok=True)
    keep = (scene.render.engine, scene.render.resolution_x, scene.render.resolution_y,
            scene.render.film_transparent, scene.display.shading.light,
            scene.display.shading.color_type, scene.display.shading.show_cavity, scene.camera)
    scene.render.engine = "BLENDER_WORKBENCH"
    sh = scene.display.shading
    sh.light, sh.color_type, sh.show_cavity = "FLAT", "SINGLE", False
    sh.single_color = (1.0, 1.0, 1.0)
    sh.show_object_outline = False
    scene.display.render_aa = "OFF"
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    hidden = []
    for o in scene.objects:
        if o != obj and o.type == "MESH" and not o.hide_render:
            o.hide_render = True
            hidden.append(o)
    cam_data = bpy.data.cameras.get("SheetCam") or bpy.data.cameras.new("SheetCam")
    cam = bpy.data.objects.get("SheetCam") or bpy.data.objects.new("SheetCam", cam_data)
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    cam_data.type = "ORTHO"
    cam_data.sensor_fit = "AUTO"
    cam_data.clip_start, cam_data.clip_end = 0.1, 100.0
    scene.camera = cam
    masks = {}
    for name, view in views.items():
        scene.render.resolution_x, scene.render.resolution_y = view.w, view.h
        scene.render.resolution_percentage = 100
        cam_data.ortho_scale = max(view.w, view.h) * view.scale
        cam.location, cam.rotation_euler = _sheet_camera(view, H)
        path = os.path.join(tmp_dir, "sil_%s.png" % name)
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(path, check_existing=False)
        px = np.empty(view.w * view.h * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        masks[name] = px.reshape(view.h, view.w, 4)[::-1, :, 3] > 0.5
        bpy.data.images.remove(img)
    for o in hidden:
        o.hide_render = False
    (scene.render.engine, scene.render.resolution_x, scene.render.resolution_y,
     scene.render.film_transparent, sh.light, sh.color_type, sh.show_cavity, scene.camera) = keep
    return masks


def silhouette_report(obj, views, H, out_dir, tag="fit"):
    """IoU of the model's silhouette against each sheet's, and a diff image per view:
    red where the model is wider than the sheet, blue where the sheet is wider than the model."""
    masks = render_silhouettes(obj, views, H, os.path.join(out_dir, "_sil"))
    report = {}
    for name, view in views.items():
        m, s = masks[name], view.mask
        inter = np.logical_and(m, s).sum()
        union = np.logical_or(m, s).sum()
        report[name] = round(float(inter) / max(union, 1), 4)
        diff = (view.rgb.astype(np.float32) * 0.55).astype(np.uint8)
        diff[m & ~s] = [235, 40, 40]
        diff[s & ~m] = [40, 90, 235]
        _save_rgb(diff, os.path.join(out_dir, "%s_silhouette_%s.png" % (tag, name)))
    return report


def _save_rgb(rgb, path):
    h, w, _ = rgb.shape
    img = bpy.data.images.new("_tmp_save", w, h, alpha=True)
    px = np.ones((h, w, 4), dtype=np.float32)
    px[..., :3] = rgb[::-1].astype(np.float32) / 255.0
    img.pixels.foreach_set(px.reshape(-1))
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


# ==================================================================================================
# Silhouette snap: the outline vertices go onto the sheets' outlines
# ==================================================================================================

def _edge_near(view, z, h, outward):
    """The sheet's outline at height z nearest model coordinate h, on the side `outward` (+1 the
    high edge, -1 the low edge) of the run containing h -- or of the nearest run if none does."""
    best, best_d = None, 1e9
    for a, b in view.runs_at(z):
        lo, hi = sorted((view.h_of(a - 0.5), view.h_of(b - 0.5)))
        d = 0.0 if lo <= h <= hi else min(abs(h - lo), abs(h - hi))
        if d < best_d:
            best, best_d = (lo, hi), d
    if best is None:
        return None
    return best[1] if outward > 0 else best[0]


def mirror_pairs(obj, tol=1e-4):
    """For each vertex, the index of its mirror image across x=0 (itself on the midline)."""
    from mathutils import kdtree
    me = obj.data
    kd = kdtree.KDTree(len(me.vertices))
    for v in me.vertices:
        kd.insert(v.co, v.index)
    kd.balance()
    pairs = np.arange(len(me.vertices))
    worst = 0.0
    for v in me.vertices:
        co, idx, dist = kd.find(Vector((-v.co.x, v.co.y, v.co.z)))
        pairs[v.index] = idx
        worst = max(worst, dist)
    return pairs, worst


def symmetrize(obj, pairs):
    """Make the mesh exactly symmetric: each vertex on the -x side becomes its partner's mirror."""
    me = obj.data
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    new = co.copy()
    for i in range(n):
        j = pairs[i]
        if j == i:
            new[i][0] = 0.0
        elif co[i][0] < 0.0:
            new[i] = (-co[j][0], co[j][1], co[j][2])
    me.vertices.foreach_set("co", new.reshape(-1))
    me.update()


def _model_edge(mask_row, view, h, outward):
    """Edge, in model units, of the model's own run nearest h on this row, on the outward side."""
    best, best_d = None, 1e9
    for a, b in sm.runs(mask_row):
        lo, hi = sorted((view.h_of(a - 0.5), view.h_of(b - 0.5)))
        d = 0.0 if lo <= h <= hi else min(abs(h - lo), abs(h - hi))
        if d < best_d:
            best, best_d = (lo, hi), d
    if best is None:
        return None
    return best[1] if outward > 0 else best[0]


def silhouette_snap(obj, body, views, cfg, pairs, out_dir):
    """Move the model's outline onto the sheets' outlines, by the measured error.

    The model's silhouette is rendered from each sheet's camera, and at every row the model's
    edge is compared with the sheet's. Each vertex near the outline -- its normal facing across
    the view -- moves by that row's error on its side, fading out as its normal turns toward or
    away from the camera. Moving the error rather than snapping to the edge matters: snapping
    every forward-facing vertex flattens the whole front onto the outermost point, which is the
    boxy visual hull. Both opposite sheets and both sides of the body are averaged, and the
    result is mirrored, so it stays symmetric. Hands (designed, not traced) and feet below the
    ankle (the sheets splay them) are left alone."""
    masks = render_silhouettes(obj, views, body.H, os.path.join(out_dir, "_sil"))
    me = obj.data
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    nor = np.empty(n * 3)
    me.vertices.foreach_get("normal", nor)
    nor = nor.reshape(n, 3)
    hand = np.zeros(n)
    for part in ("hand_l", "hand_r"):
        arr = np.empty(n)
        me.attributes["w_" + part].data.foreach_get("value", arr)
        hand = np.maximum(hand, arr)
    limit = cfg.get("snap_limit", 0.025)
    corr = np.zeros((n, 3))

    def error(view_name, z, h, outward):
        v = views[view_name]
        r = int(round(v.row(z)))
        if not (0 <= r < v.h):
            return None
        me_ = _model_edge(masks[view_name][r], v, h, outward)
        sh_ = _edge_near(v, z, h, outward)
        if me_ is None or sh_ is None:
            return None
        e = sh_ - me_
        return e if abs(e) < limit else None

    for i in range(n):
        x, y, z = co[i]
        if x < -1e-6 or hand[i] > 0.05 or z < body.ankle_z:
            continue
        nx, ny, nz = nor[i]
        horiz = math.hypot(nx, ny)
        if horiz < 1e-6:
            continue
        # front and back sheets: the outline in x
        cx = abs(nx) / horiz
        if cx > 0.8 and x > 1e-6:
            w = _smoothstep(0.8, 0.98, cx) * min(1.0, 1.2 * horiz)
            out = 1.0 if nx > 0 else -1.0
            errs = [e for e in (error("front", z, x, out), error("back", z, x, out),
                                None if error("front", z, -x, -out) is None else -error("front", z, -x, -out),
                                None if error("back", z, -x, -out) is None else -error("back", z, -x, -out))
                    if e is not None]
            if errs:
                corr[i][0] = w * float(np.mean(errs))
        # side sheets: the outline in y
        cy = abs(ny) / horiz
        if cy > 0.8:
            w = _smoothstep(0.8, 0.98, cy) * min(1.0, 1.2 * horiz)
            out = 1.0 if ny > 0 else -1.0
            errs = [e for e in (error("left", z, y, out), error("right", z, y, out)) if e is not None]
            if errs:
                corr[i][1] = w * float(np.mean(errs))
    nbrs = [[] for _ in range(n)]
    for e in me.edges:
        a, b = e.vertices
        nbrs[a].append(b)
        nbrs[b].append(a)
    for _ in range(cfg.get("snap_smooth_iterations", 2)):
        avg = np.array([corr[nb].mean(axis=0) if nb else corr[i] for i, nb in enumerate(nbrs)])
        corr = 0.5 * corr + 0.5 * avg
    me.vertices.foreach_set("co", (co + corr).reshape(-1))
    me.update()
    symmetrize(obj, pairs)
    moved = int((np.abs(corr).sum(axis=1) > 1e-5).sum())
    log("silhouette snap: %d vertices moved, largest %.1f mm" % (moved, 1000 * float(np.abs(corr).max())))
