"""
Measuring a character turnaround (task 174's final views): front, left, back and right images of
one character in an A-pose on a plain background.

Pure numpy, so it runs inside Blender (whose Python has numpy but not scipy) and outside it for
testing. Images are passed in as (H, W, 3) uint8 arrays; loading them is the caller's business.

What it produces, all in metres in the model's frame:

  * a figure mask per view, with the view registered to the model: the soles at z=0, the top of the
    head at the character's stated height, and the body's axis found in the image;
  * cross-section profiles: for a height z, the extents of the run of figure pixels that contains a
    given point, in the front view (x) and a side view (y);
  * landmarks: the heights of the neck, shoulders, armpits, crotch, wrists and fingertips, and the
    centrelines of the arms and legs.

Frames. The character faces -Y with +Z up, so its left hand is at +X. In the front view image-right
is +X; in the back view image-right is -X; in the left view (the character's own left side, facing
the image's left edge) image-right is +Y; in the right view image-right is -Y.
"""

import math

import numpy as np

VIEW_SIGN = {"front": 1.0, "back": -1.0, "left": 1.0, "right": -1.0}
VIEW_AXIS = {"front": "x", "back": "x", "left": "y", "right": "y"}


# --------------------------------------------------------------------------------------------------
# Morphology without scipy
# --------------------------------------------------------------------------------------------------

def _shift(m, dy, dx, fill):
    out = np.full_like(m, fill)
    h, w = m.shape
    ys, yd = (slice(0, h - dy), slice(dy, h)) if dy >= 0 else (slice(-dy, h), slice(0, h + dy))
    xs, xd = (slice(0, w - dx), slice(dx, w)) if dx >= 0 else (slice(-dx, w), slice(0, w + dx))
    out[yd, xd] = m[ys, xs]
    return out


def dilate(m, r):
    out = m.copy()
    for _ in range(r):
        out = out | _shift(out, 1, 0, False) | _shift(out, -1, 0, False) \
            | _shift(out, 0, 1, False) | _shift(out, 0, -1, False)
    return out


def erode(m, r):
    out = m.copy()
    for _ in range(r):
        out = out & _shift(out, 1, 0, True) & _shift(out, -1, 0, True) \
            & _shift(out, 0, 1, True) & _shift(out, 0, -1, True)
    return out


def runs(row):
    """(start, end) column pairs of the True runs in a boolean row, end exclusive."""
    d = np.diff(np.concatenate([[0], row.astype(np.int8), [0]]))
    return list(zip(np.where(d == 1)[0].tolist(), np.where(d == -1)[0].tolist()))


# --------------------------------------------------------------------------------------------------
# Segmentation
# --------------------------------------------------------------------------------------------------

def segment(rgb, distance=40, warmth=32):
    """Figure mask and skin mask.

    The backgrounds are a plain light grey with a little vignette and a soft contact shadow. The
    suit and hair are far from it in colour; skin is close to it in brightness but much warmer, so
    a pixel is figure if it is far from the border's median colour OR clearly warmer than grey.
    Highlights inside the figure (eye whites, a lit cheek) fail both tests, so the mask is closed:
    every real gap in the silhouette (arm to torso, between the legs) is far wider than the closing.
    """
    a = rgb.astype(np.int32)
    border = np.concatenate([a[:30].reshape(-1, 3), a[-30:].reshape(-1, 3),
                             a[:, :30].reshape(-1, 3), a[:, -30:].reshape(-1, 3)])
    bg = np.median(border, axis=0)
    far = np.abs(a - bg).max(axis=2) > distance
    warm = (a[..., 0] - a[..., 2]) > warmth
    fig = far | warm
    fig = dilate(erode(fig, 2), 2)          # opening: specks of noise go
    fig = erode(dilate(fig, 4), 4)          # closing: highlights inside the figure fill
    skin = fig & ((a[..., 0] - a[..., 2]) > 45) & (a[..., 0] > 120)
    return fig, skin, bg


# --------------------------------------------------------------------------------------------------
# A registered view
# --------------------------------------------------------------------------------------------------

class View:
    """One image, registered to the model's frame.

    Rows map to height by the figure's own extent: its lowest row is z=0 and its highest is the
    character's height, so each view carries its own scale and the four agree on z by construction.
    Columns map to x (front/back) or y (left/right) about axis_col, which callers set once the axis
    is known.
    """

    def __init__(self, name, rgb, height):
        self.name = name
        self.rgb = rgb
        self.height = float(height)
        self.mask, self.skin, self.bg = segment(rgb)
        rows = np.where(self.mask.any(axis=1))[0]
        cols = np.where(self.mask.any(axis=0))[0]
        self.top, self.bottom = int(rows[0]), int(rows[-1])
        self.scale = self.height / float(self.bottom - self.top)      # metres per pixel
        self.sign = VIEW_SIGN[name]
        self.axis_col = 0.5 * (cols[0] + cols[-1])
        self.h, self.w = self.mask.shape

    # pixel <-> model
    def row(self, z):
        return self.bottom - z / self.scale

    def z(self, row):
        return (self.bottom - row) * self.scale

    def col(self, h):
        return self.axis_col + self.sign * h / self.scale

    def h_of(self, col):
        return self.sign * (col - self.axis_col) * self.scale

    def runs_at(self, z, mask=None):
        m = self.mask if mask is None else mask
        r = int(round(min(max(self.row(z), self.top), self.bottom)))
        return runs(m[r])

    def extent_at(self, z, through_h, mask=None):
        """(lo, hi) in model units of the run at height z containing model coordinate through_h,
        or None. lo < hi always, whichever way this view's columns run."""
        c = self.col(through_h)
        for a, b in self.runs_at(z, mask):
            if a <= c < b:
                h0, h1 = self.h_of(a - 0.5), self.h_of(b - 0.5)
                return (min(h0, h1), max(h0, h1))
        return None

    def uv(self, h, z):
        """Model coordinates to this image's texture coordinates (v = 0 at the bottom row)."""
        return (self.col(h) + 0.5) / self.w, 1.0 - (self.row(z) + 0.5) / self.h


# --------------------------------------------------------------------------------------------------
# Measuring
# --------------------------------------------------------------------------------------------------

def _central_center(view, z0, z1, steps=24):
    """Average midpoint of the run containing the image's middle, over a band of heights."""
    mids = []
    centre_col = view.w / 2.0
    for z in np.linspace(z0, z1, steps):
        r = int(round(view.row(z)))
        for a, b in runs(view.mask[r]):
            if a <= centre_col < b:
                mids.append(0.5 * (a + b - 1))
    return float(np.median(mids)) if mids else view.axis_col


def register_axes(views, H):
    """Find each view's axis column.

    Front and back: the body's midline, the middle of the torso over the waist (between armpit and
    crotch, where the arms are clear of it). Sides: the middle of the hips' depth just above the
    crotch, which puts the model's origin under the pelvis.
    """
    for name in ("front", "back"):
        v = views[name]
        v.axis_col = _central_center(v, 0.56 * H, 0.66 * H)
    for name in ("left", "right"):
        v = views[name]
        v.axis_col = _central_center(v, 0.47 * H, 0.50 * H)


def find_landmarks(views, H):
    """Heights and centrelines, measured off the front view (and the left view for depth)."""
    f = views["front"]
    lm = {}

    def central(z):
        return f.extent_at(z, 0.0)

    def width(z):
        e = central(z)
        return (e[1] - e[0]) if e else 0.0

    zs = np.arange(0.30 * H, 0.98 * H, 0.002)
    # Crotch: scanning up from the knees, the first height where one run spans the axis.
    for z in zs:
        if z < 0.35 * H:
            continue
        if central(z) is not None:
            lm["crotch"] = float(z)
            break
    # Armpits: scanning up from the waist, the first height where the run through the axis also
    # takes in the arm -- its width jumps by more than an arm's thickness in one step.
    prev = width(0.62 * H)
    for z in np.arange(0.62 * H, 0.85 * H, 0.002):
        w = width(z)
        if w - prev > 0.04:
            lm["armpit"] = float(z)
            break
        prev = w
    # Neck: the narrowest run through the axis between the shoulders and the head.
    band = np.arange(0.80 * H, 0.92 * H, 0.002)
    widths = np.array([width(z) for z in band])
    lm["neck"] = float(band[int(np.argmin(widths))])
    lm["neck_width"] = float(widths.min())
    # Shoulder top: the highest height whose width is still most of the shoulder span.
    span = width(lm["armpit"] + 0.02)
    for z in np.arange(lm["neck"], lm["armpit"], -0.002):
        if width(z) >= 0.80 * span:
            lm["shoulder_top"] = float(z)
            break
    lm["shoulder_span"] = float(span)
    # Head: its widest point above the neck.
    band = np.arange(lm["neck"], H, 0.002)
    widths = np.array([width(z) for z in band])
    lm["head_wide"] = float(band[int(np.argmax(widths))])
    lm["head_width"] = float(widths.max())
    # Chin: in the left view, the front of the face steps back to the neck. The largest jump in the
    # front extent between the neck and the widest part of the head.
    s = views["left"]
    band = np.arange(lm["neck"], lm["head_wide"], 0.002)
    fronts = []
    for z in band:
        e = s.extent_at(z, 0.0)
        fronts.append(e[0] if e else 0.0)
    fronts = np.array(fronts)
    jump = fronts[:-1] - fronts[1:]          # positive where the front comes forward going up
    lm["chin"] = float(band[int(np.argmax(jump)) + 1])
    lm.update(_arms(f, lm, H))
    lm.update(_legs(f, lm, H))
    return lm


def _arms(f, lm, H):
    """Arm centrelines in the front view, per side, as model-space (x, z) points.

    Below the armpit each arm is its own run, and the midpoint of a horizontal cut through an
    inclined limb lies on its axis. A line fitted through those midpoints from the armpit to the
    wrist is the arm's axis; the wrist is where the skin starts, measured from the skin mask.
    """
    out = {}
    torso = lambda z: f.extent_at(z, 0.0)
    for side, sgn in (("l", 1.0), ("r", -1.0)):
        pts = []
        for z in np.arange(lm["armpit"] - 0.02, 0.40 * H, -0.004):
            t = torso(z)
            if t is None:
                continue
            cand = []
            for a, b in f.runs_at(z):
                h0, h1 = sorted((f.h_of(a - 0.5), f.h_of(b - 0.5)))
                mid = 0.5 * (h0 + h1)
                if sgn * mid > sgn * (t[1] if sgn > 0 else t[0]) and abs(mid) < 0.6 * H:
                    cand.append((abs(mid - (t[1] if sgn > 0 else t[0])), h0, h1, mid))
            if not cand:
                continue
            _, h0, h1, mid = min(cand)
            pts.append((z, mid, h0, h1))
        # skin: the hand. Its top is the wrist.
        skin_z = []
        for z, mid, h0, h1 in pts:
            r = int(round(f.row(z)))
            c0, c1 = sorted((f.col(h0), f.col(h1)))
            if f.skin[r, int(c0):int(c1) + 1].mean() > 0.5:
                skin_z.append(z)
        wrist = max(skin_z) if skin_z else 0.55 * H
        tip = min(z for z, *_ in pts)
        arm = [(z, mid) for z, mid, *_ in pts if z > wrist + 0.01]
        wrist_from_skin = bool(skin_z) and len(arm) >= 3
        if not wrist_from_skin:
            # "Skin" is any warm colour, and the Martian's terracotta suit is warm while his skin is pale: the
            # whole arm read as hand and left nothing above the wrist to fit (182). Fit the whole arm instead,
            # and leave the wrist unmeasured rather than crash.
            wrist = 0.55 * H
            arm = [(z, mid) for z, mid, *_ in pts]
        zz = np.array([p[0] for p in arm])
        xx = np.array([p[1] for p in arm])
        k, c = np.polyfit(zz, xx, 1)             # x = k z + c along the arm
        out["arm_" + side] = {"k": float(k), "c": float(c), "wrist_z": float(wrist),
                              "wrist_from_skin": wrist_from_skin, "tip_z": float(tip),
                              "samples": [(float(z), float(m), float(a), float(b))
                                          for z, m, a, b in pts]}
    return out


def _legs(f, lm, H):
    """Leg centrelines in the front view, per side: midpoints of each leg's run below the crotch."""
    out = {}
    for side, sgn in (("l", 1.0), ("r", -1.0)):
        pts = []
        for z in np.arange(lm["crotch"] - 0.01, 0.0, -0.004):
            best = None
            for a, b in f.runs_at(z):
                h0, h1 = sorted((f.h_of(a - 0.5), f.h_of(b - 0.5)))
                mid = 0.5 * (h0 + h1)
                if sgn * mid > 0 and abs(mid) < 0.25 * H:
                    if best is None or abs(mid) < abs(best[2]):
                        best = (h0, h1, mid)
            if best:
                pts.append((float(z), best[2], best[0], best[1]))
        out["leg_" + side] = {"samples": pts}
    return out
