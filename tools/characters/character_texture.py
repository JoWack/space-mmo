"""
UVs, the baked colour texture, and the material, for a character built by character_build.

The texture is the four sheets projected onto the surface. Each texel takes its colour from the
sheets that could actually see it, weighted by how squarely: the weights are baked by Cycles as
the direct light from one sun lamp per sheet direction, which is exactly "visible from that view,
times the cosine". So the torso's side behind a hanging arm is not painted with the arm. The sheets
are levelled to one another before they are blended (see compose), and a texel no sheet sees well
takes its colour from its surroundings.
"""

import math
import os
import time

import bmesh
import bpy
import numpy as np

from sheet_measure import dilate as sm_dilate, erode as sm_erode


def log(msg):
    print("[character] " + msg, flush=True)


# ==================================================================================================
# UVs
# ==================================================================================================

def edit_mode(obj, on):
    bpy.context.view_layer.objects.active = obj
    for o in bpy.context.view_layer.objects:
        o.select_set(o == obj)   # == not is: Blender returns a fresh wrapper per access
    if on and obj.mode != "EDIT":
        bpy.ops.object.mode_set(mode="EDIT")
    elif not on and obj.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")


def _islands(faces):
    """Split faces into islands joined across non-seam edges."""
    pending = set(faces)
    out = []
    while pending:
        seed = pending.pop()
        island, stack = {seed}, [seed]
        while stack:
            f = stack.pop()
            for e in f.edges:
                if e.seam:
                    continue
                for g in e.link_faces:
                    if g in pending:
                        pending.discard(g)
                        island.add(g)
                        stack.append(g)
        out.append(island)
    return out


def unwrap(obj, body, cfg):
    """Unwrap along the seams marked on the cage, give the face and hands more texels, pack.

    Islands: the head and neck (cut at the collar and down the back of the head), the torso (cut
    down the back and round the arm and leg roots), each arm (cut along the inside), each palm,
    each finger, each leg (cut along the inside) and each sole."""
    me = obj.data
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    edit_mode(obj, True)
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.unwrap(method="ANGLE_BASED", margin=0.002)
    bm = bmesh.from_edit_mesh(me)
    uv = bm.loops.layers.uv.active
    hand_layers = [bm.verts.layers.float.get("w_hand_l"), bm.verts.layers.float.get("w_hand_r")]
    neck = body.lm["neck"]
    groups = {"head": [], "hand": []}
    for f in bm.faces:
        is_hand = any(lay is not None and max(v[lay] for v in f.verts) > 0.5 for lay in hand_layers)
        if is_hand:
            groups["hand"].append(f)
        elif f.calc_center_median().z > neck - 0.01:
            groups["head"].append(f)
    for key, scale in (("head", cfg.get("uv_head_scale", 1.6)), ("hand", cfg.get("uv_hand_scale", 1.3))):
        for island in _islands(groups[key]):
            pts = [l[uv].uv.copy() for f in island for l in f.loops]
            cx = sum(p.x for p in pts) / len(pts)
            cy = sum(p.y for p in pts) / len(pts)
            for f in island:
                for l in f.loops:
                    l[uv].uv.x = cx + (l[uv].uv.x - cx) * scale
                    l[uv].uv.y = cy + (l[uv].uv.y - cy) * scale
    bmesh.update_edit_mesh(me)
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    kwargs = dict(rotate=True, scale=True, margin=cfg.get("uv_margin", 0.006))
    try:
        bpy.ops.uv.pack_islands(margin_method="SCALED", shape_method="CONCAVE", **kwargs)
    except TypeError:
        bpy.ops.uv.pack_islands(**kwargs)
    edit_mode(obj, False)
    return uv_report(obj)


def uv_report(obj):
    """How much of the square the islands use, how many corners fall outside it, and how many
    islands there are."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    uvl = bm.loops.layers.uv.active
    area, outside = 0.0, 0
    for f in bm.faces:
        pts = [l[uvl].uv for l in f.loops]
        a = 0.0
        for i in range(len(pts)):
            p, q = pts[i], pts[(i + 1) % len(pts)]
            a += p.x * q.y - q.x * p.y
            if not (-1e-4 <= p.x <= 1 + 1e-4 and -1e-4 <= p.y <= 1 + 1e-4):
                outside += 1
        area += abs(a) * 0.5
    islands = len(_islands(list(bm.faces)))
    bm.free()
    return {"coverage": round(area, 4), "corners_outside": outside, "islands": islands}


# ==================================================================================================
# Baking
# ==================================================================================================

def _image_array(img):
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    return px.reshape(h, w, 4)


def _bake(obj, kind, image, margin=0, pass_filter=None, emit_color=None, emit_attributes=()):
    """One Cycles bake into `image` through a throwaway material whose active node targets it.
    An EMIT bake emits `emit_color`, at a strength of the sum of `emit_attributes` if any."""
    mat = bpy.data.materials.new("_bake")
    mat.use_nodes = True
    nt = mat.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    for n in nt.nodes:
        n.select = (n == tex)   # == not is: Blender returns a fresh wrapper per access
    nt.nodes.active = tex
    if emit_color is not None:
        out = nt.nodes.get("Material Output")
        em = nt.nodes.new("ShaderNodeEmission")
        em.inputs["Color"].default_value = emit_color
        total = None
        for name in emit_attributes:
            at = nt.nodes.new("ShaderNodeAttribute")
            at.attribute_name = name
            if total is None:
                total = at.outputs["Fac"]
            else:
                add = nt.nodes.new("ShaderNodeMath")
                add.operation = "ADD"
                nt.links.new(total, add.inputs[0])
                nt.links.new(at.outputs["Fac"], add.inputs[1])
                total = add.outputs[0]
        if total is not None:
            nt.links.new(total, em.inputs["Strength"])
        nt.links.new(em.outputs[0], out.inputs[0])
    keep = list(obj.data.materials)
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    edit_mode(obj, False)
    kwargs = dict(type=kind, margin=margin, use_clear=True)
    if pass_filter is not None:
        kwargs["pass_filter"] = pass_filter
    bpy.ops.object.bake(**kwargs)
    obj.data.materials.clear()
    for m in keep:
        obj.data.materials.append(m)
    bpy.data.materials.remove(mat)
    return _image_array(image)


def _sample(rgb, mask, cols, rows):
    """Bilinear samples of an (H, W, 3) image at float pixel coordinates, and whether all four
    neighbours are figure -- a sample straddling the outline is treated as unseen, so the grey
    background never bleeds onto the model's silhouette edges."""
    h, w, _ = rgb.shape
    c0 = np.clip(np.floor(cols).astype(np.int64), 0, w - 2)
    r0 = np.clip(np.floor(rows).astype(np.int64), 0, h - 2)
    fc = np.clip(cols - c0, 0, 1)[:, None]
    fr = np.clip(rows - r0, 0, 1)[:, None]
    img = rgb.astype(np.float32)
    top = img[r0, c0] * (1 - fc) + img[r0, c0 + 1] * fc
    bot = img[r0 + 1, c0] * (1 - fc) + img[r0 + 1, c0 + 1] * fc
    col = top * (1 - fr) + bot * fr
    inside = mask[r0, c0] & mask[r0, c0 + 1] & mask[r0 + 1, c0] & mask[r0 + 1, c0 + 1]
    inside &= (cols >= 0) & (cols < w - 1) & (rows >= 0) & (rows < h - 1)
    return col, inside


def _dilate(rgb, covered, steps):
    """Push colours outward from covered texels into the gutters between islands, so filtering and
    mipmaps never pull black into the seams."""
    out = rgb.copy()
    cov = covered.copy()
    h, w = cov.shape

    def shifted(a, dy, dx, fill):
        # a shift that does not wrap round the image's edges, unlike np.roll
        res = np.full_like(a, fill)
        ys, yd = (slice(0, h - dy), slice(dy, h)) if dy >= 0 else (slice(-dy, h), slice(0, h + dy))
        xs, xd = (slice(0, w - dx), slice(dx, w)) if dx >= 0 else (slice(-dx, w), slice(0, w + dx))
        res[yd, xd] = a[ys, xs]
        return res

    for _ in range(steps):
        if cov.all():
            break
        acc = np.zeros_like(out)
        cnt = np.zeros(cov.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            sc = shifted(cov, dy, dx, False)
            sv = shifted(out, dy, dx, 0.0)
            acc += sv * sc[..., None]
            cnt += sc
        grow = (~cov) & (cnt > 0)
        out[grow] = acc[grow] / cnt[grow][:, None]
        cov = cov | grow
    return out


# ==================================================================================================
# Blending the sheets
# ==================================================================================================
#
# Each sheet is a separate render with its own exposure and its own baked lighting, so one patch of
# suit is a slightly different colour in each. Blending the images as they are leaves a band
# wherever one sheet hands over to the next -- down the side of the torso, round each thigh, along
# the cheek -- and a patch no sheet saw well comes out a flat colour with a muddy edge.
#
# So every sheet is levelled to the others before any detail is taken from it:
#
#   blend   every sheet's colour, weighted broadly (cos^2), averaged over a few centimetres in 3D:
#           the colour the sheets agree on, with no hand-over anywhere in it.
#   level   the same average of one sheet alone. blend - level is a smooth correction, and adding
#           it brings that sheet's exposure and lighting to the agreed colour while keeping its
#           detail exactly.
#   detail  the levelled sheets, weighted sharply (cos^8): fine detail comes from the one sheet
#           that saw each spot most squarely, so it does not ghost, and a hand-over between two
#           levelled sheets changes nothing but which one drew the fine lines.
#
# The averages are taken in 3D rather than across the texture, so they run straight over UV seams,
# and region by region (head, hands, suit), so skin and suit never bleed into each other. A texel no
# sheet saw well takes the blend from its surroundings, at the finest scale that has any.

def _box_mean(a, r, axis):
    """Mean over 2r+1 cells along one axis, counting cells beyond the edge as zero."""
    a = np.moveaxis(a, axis, 0)
    n = a.shape[0]
    c = np.zeros((n + 2 * r + 1,) + a.shape[1:], dtype=np.float64)
    c[r + 1:r + 1 + n] = a
    np.cumsum(c, axis=0, out=c)
    return np.moveaxis((c[2 * r + 1:] - c[:n]) / (2 * r + 1), 0, axis)


class _Grid:
    """Points binned into cubes, for averaging a field over 3D neighbourhoods.

    A field is averaged by summing weight * value and weight into the cells, box-filtering both
    three times along each axis (near enough a Gaussian of sigma 2.5 cells at radius 2), reading
    them back trilinearly and dividing: normalised convolution, so empty space and unseen texels
    count for nothing rather than for black."""

    def __init__(self, P, cell, radius=2, passes=3):
        self.radius, self.passes = radius, passes
        pad = (radius * passes + 2) * cell
        lo = P.min(axis=0) - pad
        self.shape = tuple(int(n) for n in np.ceil((P.max(axis=0) + pad - lo) / cell))
        top = np.array(self.shape) - 1
        self.flat = np.ravel_multi_index(
            np.clip(np.floor((P - lo) / cell).astype(np.int64), 0, top).T, self.shape)
        g = (P - lo) / cell - 0.5                        # cell centres sit at (i + 0.5) * cell
        self.i0 = np.clip(np.floor(g).astype(np.int64), 0, top - 1)
        self.t = np.clip(g - self.i0, 0.0, 1.0)

    def average(self, values, weights):
        """(averaged values (N, C), local weight (N,)) at every point."""
        n_cells, C = int(np.prod(self.shape)), values.shape[1]
        acc = np.empty(self.shape + (C + 1,))
        for c in range(C):
            acc[..., c] = np.bincount(self.flat, weights * values[:, c], n_cells).reshape(self.shape)
        acc[..., C] = np.bincount(self.flat, weights, n_cells).reshape(self.shape)
        for _ in range(self.passes):
            for axis in range(3):
                acc = _box_mean(acc, self.radius, axis)
        out = np.zeros((len(self.flat), C + 1))
        for corner in range(8):
            d = np.array([(corner >> k) & 1 for k in range(3)])
            i = self.i0 + d
            out += acc[i[:, 0], i[:, 1], i[:, 2]] * np.prod(np.where(d, self.t, 1.0 - self.t), axis=1)[:, None]
        return out[:, :C] / np.maximum(out[:, C], 1e-12)[:, None], out[:, C]


def _smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def compose(P, vis, views, body, cfg, hands):
    """The colour of every texel, from its position P (N, 3), its visibility from each sheet, and
    whether it is hand (N,) bool.

    Returns (colours (N, 3) in 0..255, {view: fraction of texels it saw}, the fraction of texels no
    sheet saw well enough to colour, which took their colour from their surroundings instead)."""
    t0 = time.time()
    N = len(P)
    if not hands.any():
        raise RuntimeError("no texel is hand: the mesh has lost its w_hand_* part weights")
    # Regions: the hands, the head and neck above the collar (where the sheets show skin), and the
    # suit everywhere else.
    head = (P[:, 2] > body.collar_z - 0.004) & ~hands
    suit = ~head & ~hands
    t = np.clip((P[:, 2] - body.collar_z) / 0.05, 0.0, 1.0)
    shift = t * t * (3.0 - 2.0 * t)
    p_blend, p_detail = cfg.get("blend_power", 2.0), cfg.get("detail_power", 8.0)
    sampled = {}
    for name, v in views.items():
        h = P[:, 0] if name in ("front", "back") else P[:, 1]
        # each side sheet's head is shifted onto the model's (see Body._collar_and_head_offsets)
        h = h + shift * body.head_offset.get(name, 0.0)
        cols = v.axis_col + v.sign * h / v.scale
        rows = v.bottom - P[:, 2] / v.scale
        # Sampling is only trusted a few pixels inside a sheet's figure: right at the outline the
        # mask's closing takes in a little background and contact shadow, which reads as grey
        # patches on the toes.
        c, inside = _sample(v.rgb, sm_erode(v.mask, 3), cols, rows)
        skin_near, _ = _sample(sm_dilate(v.skin, 4)[..., None].astype(np.float32), v.mask, cols, rows)
        skin_core, _ = _sample(sm_erode(v.skin, 2)[..., None].astype(np.float32), v.mask, cols, rows)
        ok = inside.astype(np.float64)
        ok[P[:, 2] < 0.01] = 0.0                          # soles: nobody saw them
        # A suit texel never takes a skin pixel: in the side sheets a hand hangs over the hip, and
        # the model's hand does not hide exactly the same spot, so without this the hip wears a
        # streak of the sheet's hand. A hand texel takes nothing but skin: the model's fingers are
        # designed rather than traced, and where one crosses a gap between the sheet's fingers it
        # would otherwise wear a dark mark.
        ok[suit] *= 1.0 - skin_near[suit, 0]
        ok[hands] *= skin_core[hands, 0]
        sampled[name] = (c.astype(np.float64), ok)
    # The front sheet owns the face: everywhere on the head it sees within about 65 degrees of
    # square, the other sheets are not used at all. They are separate drawings of the face, not
    # views of one -- the left sheet has sideburns and stubble the front sheet does not -- and any
    # blend of two of them on a cheek is a dirty smudge, however well their tones are matched.
    lo_cos, hi_cos = cfg.get("face_front_cos", (0.34, 0.5))
    own = np.where(head, _smoothstep(lo_cos, hi_cos, vis["front"]) * sampled["front"][1], 0.0)
    sheets = {}
    for name, (c, ok) in sampled.items():
        if name != "front":
            ok = ok * (1.0 - own)
        cos = np.clip(vis[name], 0.0, 1.0)
        detail_w = ok * cos ** p_detail
        # Where the face hands over to the side sheets, the front sheet's detail still leads: the
        # side sheets' features sit a centimetre off the model's.
        if name == "front":
            detail_w[head] *= cfg.get("face_front_boost", 4.0)
        sheets[name] = (c, ok * cos ** p_blend, detail_w)

    region_colour = np.tile(np.array(cfg["fallback_suit"], dtype=np.float64), (N, 1))
    neck = body.lm["neck"]
    region_colour[P[:, 2] > neck] = cfg["fallback_skin"]
    region_colour[P[:, 2] > neck + 0.55 * (body.H - neck)] = cfg["fallback_hair"]
    region_colour[hands] = cfg["fallback_skin"]

    cells = cfg.get("level_cell", {"head": 0.005, "hands": 0.005, "suit": 0.008})
    fill_cells = cfg.get("fill_cells", [0.015, 0.04])
    lo, hi = cfg.get("seen_range", (0.02, 0.15))
    out = np.zeros((N, 3))
    seen_well = np.zeros(N)
    for region, sel in (("head", head), ("hands", hands), ("suit", suit)):
        if not sel.any():
            continue
        Q = P[sel]
        fine = _Grid(Q, cells[region])
        level, local = {}, {}
        for name, (c, blend_w, _) in sheets.items():
            level[name], local[name] = fine.average(c[sel], blend_w[sel])
        local_total = sum(local.values())
        blend = sum(level[k] * local[k][:, None] for k in level) / np.maximum(local_total, 1e-12)[:, None]
        num, den, blend_total = np.zeros((len(Q), 3)), np.zeros(len(Q)), np.zeros(len(Q))
        for name, (c, blend_w, detail_w) in sheets.items():
            num += (c[sel] + blend - level[name]) * detail_w[sel][:, None]
            den += detail_w[sel]
            blend_total += blend_w[sel]
        detail = num / np.maximum(den, 1e-12)[:, None]
        # The smooth field, finest scale first wherever it has support, coarser where it has not.
        colour_sum = sum(c[sel] * w[sel][:, None] for c, w, _ in sheets.values())
        colour_avg = colour_sum / np.maximum(blend_total, 1e-12)[:, None]
        layers = [(blend, local_total)] + [_Grid(Q, cell).average(colour_avg, blend_total)
                                           for cell in fill_cells]
        smooth = region_colour[sel]
        for value, weight in reversed(layers):
            ref = np.percentile(weight[weight > 0], 90) if (weight > 0).any() else 1.0
            a = np.clip(weight / (cfg.get("fill_support", 0.15) * ref), 0.0, 1.0)[:, None]
            smooth = a * value + (1.0 - a) * smooth
        seen = _smoothstep(lo, hi, blend_total) * (den > 1e-12)
        colour = seen[:, None] * detail + (1.0 - seen[:, None]) * smooth
        if region == "hands":
            # The sheet's knuckles and creases do not sit on the model's designed fingers.
            colour = smooth + cfg.get("hand_detail", 0.35) * (colour - smooth)
        elif region == "suit" and cfg.get("suit_shading_keep", 1.0) < 1.0:
            # The sheets' studio lighting is baked into the suit, and the game lights it again.
            # Shading between a couple of centimetres and a decimetre is mostly that lighting;
            # anything broader is the suit's own colours (boots, panels) and stays.
            value, weight = layers[-1]
            broad = np.where((weight > 1e-9)[:, None], value, smooth)
            colour = colour - (1.0 - cfg["suit_shading_keep"]) * (smooth - broad)
        out[sel] = colour
        seen_well[sel] = seen
    per_view = {name: float((blend_w > 0.0625).mean()) for name, (_, blend_w, _) in sheets.items()}
    log("compose: %.1fs" % (time.time() - t0))
    return np.clip(out, 0.0, 255.0), per_view, float((seen_well < 0.5).mean())


def _fresh_image(name, size, float_buffer=True):
    img = bpy.data.images.get(name)
    if img is not None:
        bpy.data.images.remove(img)
    img = bpy.data.images.new(name, size, size, alpha=True, float_buffer=float_buffer)
    img.colorspace_settings.name = "Non-Color"
    return img


SUN_ROTATIONS = {"front": (90, 0, 0), "back": (-90, 0, 0), "left": (90, 0, 90), "right": (90, 0, -90)}


def bake_texture(obj, body, views, cfg, size, out_path):
    """Bake the colour texture and save it as an 8-bit sRGB PNG. Returns the Blender image."""
    scene = bpy.context.scene
    keep_engine, keep_world = scene.render.engine, scene.world
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = cfg.get("bake_samples", 4)
    world = bpy.data.worlds.get("_bake_world") or bpy.data.worlds.new("_bake_world")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg is not None:
        bg.inputs["Strength"].default_value = 0.0
    scene.world = world
    lights_hidden = [o for o in scene.objects if o.type == "LIGHT" and not o.hide_render]
    for o in lights_hidden:
        o.hide_render = True
    t0 = time.time()
    # Blender 5.2's POSITION bake SUMS its samples where every other bake averages them: at 4
    # samples every position comes back four times too far from the origin, and every texel then
    # projects off the sheets and silently falls back to a flat colour. Measured on a unit cube at
    # x=2: 1 sample gives x 1.5..2.5, 2 gives 3..5, 4 gives 6..10. At one sample, sum and average
    # agree, and the guard below refuses to go on if the baked positions do not fit the mesh.
    samples = scene.cycles.samples
    scene.cycles.samples = 1
    pos = _bake(obj, "POSITION", _fresh_image("_bake_pos", size))[..., :3]
    scene.cycles.samples = samples
    cov = _bake(obj, "EMIT", _fresh_image("_bake_cov", size), emit_color=(1, 1, 1, 1))[..., 0] > 0.5
    # Which texels are hand, from the mesh's own part weights. A box round the wrist missed the
    # thumb, which reaches in front of the palm, and its tip came out the suit's navy.
    hand = _bake(obj, "EMIT", _fresh_image("_bake_hand", size), emit_color=(1, 1, 1, 1),
                 emit_attributes=("w_hand_l", "w_hand_r"))[..., 0]
    lo = np.array([min(v.co[i] for v in obj.data.vertices) for i in range(3)])
    hi = np.array([max(v.co[i] for v in obj.data.vertices) for i in range(3)])
    blo, bhi = pos[cov].min(axis=0), pos[cov].max(axis=0)
    if np.abs(blo - lo).max() > 0.02 or np.abs(bhi - hi).max() > 0.02:
        raise RuntimeError("baked positions span %s..%s but the mesh spans %s..%s: the POSITION "
                           "bake is not returning model coordinates" % (blo, bhi, lo, hi))
    vis = {}
    for name, rot in SUN_ROTATIONS.items():
        ld = bpy.data.lights.new("_sun_" + name, "SUN")
        ld.energy = 1.0
        ld.angle = 0.0
        lo = bpy.data.objects.new("_sun_" + name, ld)
        lo.rotation_euler = tuple(math.radians(a) for a in rot)
        scene.collection.objects.link(lo)
        raw = _bake(obj, "DIFFUSE", _fresh_image("_bake_vis_" + name, size),
                    pass_filter={"DIRECT"})[..., 0]
        # normalised so a texel facing the sun squarely is 1: the weight is then cos^power
        peak = float(np.percentile(raw[cov], 99.5)) if cov.any() else 1.0
        vis[name] = np.clip(raw / max(peak, 1e-6), 0.0, 1.0)
        bpy.data.objects.remove(lo, do_unlink=True)
        bpy.data.lights.remove(ld)
    log("bakes: position, coverage and four visibility maps in %.1fs" % (time.time() - t0))

    P = pos[cov]
    final, per_view, unseen = compose(P, {k: v[cov] for k, v in vis.items()}, views, body, cfg,
                                      hand[cov] > 0.5)
    tex = np.zeros((size, size, 3))
    tex[cov] = final
    tex = _dilate(tex, cov, 32)
    log("texture: %d texels; seen by front %.0f%%, back %.0f%%, left %.0f%%, right %.0f%%; "
        "%.1f%% seen by no sheet, coloured from their surroundings" % (
            len(P), 100 * per_view["front"], 100 * per_view["back"], 100 * per_view["left"],
            100 * per_view["right"], 100.0 * unseen))
    name = os.path.splitext(os.path.basename(out_path))[0]
    old = bpy.data.images.get(name)
    if old is not None:
        bpy.data.images.remove(old)
    img = bpy.data.images.new(name, size, size, alpha=False)
    rgba = np.ones((size, size, 4), dtype=np.float32)
    # the baked arrays are already in Blender's row order (bottom row first), so no flip here
    rgba[..., :3] = np.clip(tex / 255.0, 0, 1)
    img.pixels.foreach_set(rgba.reshape(-1))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.filepath_raw = out_path
    img.file_format = "PNG"
    img.save()
    img.colorspace_settings.name = "sRGB"
    for n in ["_bake_pos", "_bake_cov", "_bake_hand"] + ["_bake_vis_" + k for k in SUN_ROTATIONS]:
        im = bpy.data.images.get(n)
        if im is not None:
            bpy.data.images.remove(im)
    for o in lights_hidden:
        o.hide_render = False
    scene.world = keep_world
    scene.render.engine = keep_engine
    report = {"texels": int(len(P)), "fallback": unseen,
              "seen": {k: round(v, 3) for k, v in per_view.items()}}
    return img, report


def make_material(obj, name, image, roughness):
    mat = bpy.data.materials.get(name)
    if mat is not None:
        bpy.data.materials.remove(mat)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.location = (-420, 260)
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = roughness
    if "Specular IOR Level" in bsdf.inputs:
        bsdf.inputs["Specular IOR Level"].default_value = 0.35
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    return mat
