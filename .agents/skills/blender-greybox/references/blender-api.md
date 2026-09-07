# Headless Blender

Run everything through the CLI. Find the executable before assuming a path —
on Windows it is often a Steam install on a non-system drive, and the registry
uninstall keys list it:

```bash
blender --background --python build_model.py -- --out <dir> --fbx <path>
```

Check `blender --version` once. The API moves; 4.x and 5.x differ in render
engine identifiers and colour management options.

## Build meshes from data, not from operators

`bpy.ops.mesh.primitive_cube_add` depends on context that barely exists in
background mode, and it is slow at volume. Build the mesh directly:

```python
mesh = bpy.data.meshes.new(name)
mesh.from_pydata(verts, [], faces)
mesh.validate()
mesh.update()
obj = bpy.data.objects.new(name, mesh)
collection.objects.link(obj)
```

Winding matters for outward normals. For a box with vertices 0–3 as the bottom
ring and 4–7 as the top, in the same order:

```python
((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
 (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
```

One mesh object per material, with many solids merged into it, keeps the
outliner readable and the export tidy. Do not merge across materials — the
coincidence check grades by material, and it needs them separate to tell a
flicker from a harmless tie.

## Units

Set them explicitly, once:

```python
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 1.0
scene.unit_settings.length_unit = "METERS"
```

Then one Blender unit is one metre and the numbers in the script are the
numbers on the drawing.

## Render engines

Query rather than assume, because the identifiers have changed across versions:

```bash
blender --background --python-expr "import bpy; print([i.identifier for i in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items])"
```

Workbench (`BLENDER_WORKBENCH`) is not always in that list even when it works —
it registers separately. EEVEE is `BLENDER_EEVEE` in 5.x, having been
`BLENDER_EEVEE_NEXT` for part of 4.x.

### Workbench for drawings

Right for orthographic plans and cutaways: fast, flat, and it reads like a
drawing. Use `color_type = "MATERIAL"` (which reads `material.diffuse_color`,
not the node tree), cavity on, object outline on.

**Wrong for interiors.** The studio light is view-space, so a ceiling seen from
below faces away from it and renders black. An eye-level shot inside a building
comes out as a dark box with a black lid, which looks like a modelling fault
and is not one.

### EEVEE for interiors

EEVEE gives no bounce light here, so an interior lit only by ceiling panels
goes black on every vertical surface. The world is what fixes that:

- World background around 0.5 grey at strength ~0.55, doing the work the bounce
  would.
- Area light panels at modest wattage for shape — a few thousand watts over a
  large room, not tens of thousands. Overpowering them washes everything to
  white, which is just as unreadable as black.
- **`obj.visible_camera = False` on every panel.** An area light in frame blows
  out the shot.
- `view_transform = "AgX"` rolls highlights off instead of clipping them.
  Exposure around −0.35 as a starting point.

Expect two or three iterations on exposure. Render, look at the image, adjust.

## Cameras

Blender's camera looks down −Z. Rotating +90° about X aims it along +Y:

```python
cam.rotation_euler = (math.radians(90), 0, 0)   # level, facing +Y
```

Pitch **below** 90° looks down; above 90° looks up. Getting this backwards
produces a ceiling shot that looks like the model is missing.

For a target-pointed camera:

```python
d = Vector(target) - obj.location
obj.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
```

Orthographic top-down: `cam_data.type = "ORTHO"`, `ortho_scale` a little larger
than the model's longest side.

### Framing interiors

Eye-level shots are the ones that tell you whether the space works, and they
are easy to frame uselessly:

- **Stand where a person would**, at eye height, and look where they would look.
- **A railing or parapet occludes everything below it** from more than a step
  back. To show a drop, put the camera at the rail and pitch down.
- **A wide opening seen from far back fills the frame** and the framing is lost.
  Back up until the opening reads as an aperture, or accept that the reveal
  happens as you walk through it.

## Saving

`bpy.ops.wm.save_as_mainfile(filepath=...)` writes the `.blend`. It also leaves
a `.blend1` backup beside it — ignore it in version control.

Hide anything that gets in the way of the first look (a roof, usually) with
`obj.hide_viewport = True` **after** exporting, since the FBX exporter skips
hidden objects.
