# Renders DSS1's app icon with Blender (Cycles): a hi-fi speaker driver set in a walnut and brushed-aluminium
# block, with DSS1 printed on the cone. Lit by the same warm key light as the parts.
#
#   python3 tools/render/make_icon_print.py /tmp/cone_print.png     (DSS1 printed for the cone, in the app's fonts)
#   /tmp/bvenv/bin/python -I tools/render/render_icon.py assets/icon /tmp/cone_print.png
#
# Writes assets/icon/dss1_icon.png (1024 x 1024, transparent outside the rounded square).
import bpy, math, sys, os
from mathutils import Vector

OUT = sys.argv[1] if len(sys.argv) > 1 else 'assets/icon'
PRINT = sys.argv[2] if len(sys.argv) > 2 else '/tmp/cone_print.png'
RENDER = os.path.join(os.path.dirname(OUT.rstrip('/')), 'render')
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 96; sc.cycles.use_denoising = True
sc.render.film_transparent = True
sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'
sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
sc.render.resolution_x = sc.render.resolution_y = 1024
world = bpy.data.worlds.new('W'); sc.world = world; world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.30, 0.29, 0.28, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.3

def area(loc, energy, size, color=(1, 1, 1)):
    bpy.ops.object.light_add(type='AREA', location=loc)
    lt = bpy.context.active_object; lt.data.energy = energy; lt.data.size = size; lt.data.color = color
    lt.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
area((-7, 7, 11), 1600, 6, (1.0, 0.90, 0.78))   # warm key, top left
area((8, -4, 6), 380, 5, (0.75, 0.85, 1.0))      # cool fill
area((0, 0, 14), 260, 10)

bpy.ops.object.camera_add(location=(0, -1.2, 30), rotation=(math.radians(2.3), 0, 0))
cam = bpy.context.active_object; cam.data.lens = 110; cam.data.sensor_width = 36
sc.camera = cam

def mat(name, color, metallic=0.0, rough=0.4, coat=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*color, 1); b.inputs['Metallic'].default_value = metallic
    b.inputs['Roughness'].default_value = rough; b.inputs['Coat Weight'].default_value = coat
    return m

def image_tex(m, path, scale=(1, 1, 1), loc=(0, 0, 0), to='Base Color', proj='Object'):
    n = m.node_tree.nodes; l = m.node_tree.links; b = n['Principled BSDF']
    tc = n.new('ShaderNodeTexCoord'); mp = n.new('ShaderNodeMapping'); tx = n.new('ShaderNodeTexImage')
    tx.image = bpy.data.images.load(path)
    mp.inputs['Scale'].default_value = scale; mp.inputs['Location'].default_value = loc
    l.new(tc.outputs[proj], mp.inputs['Vector']); l.new(mp.outputs['Vector'], tx.inputs['Vector'])
    return n, l, b, tx

def spun(m, amount=0.85):
    n = m.node_tree.nodes; l = m.node_tree.links; b = n['Principled BSDF']
    t = n.new('ShaderNodeTangent'); t.direction_type = 'RADIAL'; t.axis = 'Z'
    b.inputs['Anisotropic'].default_value = amount; l.new(t.outputs['Tangent'], b.inputs['Tangent'])

# --- The block: walnut, with a brushed aluminium face. Apple's grid: 824 of 1024 for the rounded square.
SIZE = 8.0

def rounded_slab(size, radius, thickness, top, bevel=0.06):
    bpy.ops.mesh.primitive_plane_add(size=size, location=(0, 0, top))
    o = bpy.context.active_object
    r = o.modifiers.new('round', 'BEVEL'); r.affect = 'VERTICES'; r.width = radius; r.segments = 32
    so = o.modifiers.new('solid', 'SOLIDIFY'); so.thickness = thickness; so.offset = -1
    e = o.modifiers.new('edge', 'BEVEL'); e.width = bevel; e.segments = 4; e.limit_method = 'ANGLE'
    return o

body = rounded_slab(SIZE, 1.75, 1.2, 0.0, 0.12)
walnut = mat('walnut', (0.16, 0.08, 0.04), rough=0.62, coat=0.15)
n, l, b, tx = image_tex(walnut, os.path.join(RENDER, 'walnut.png'), scale=(0.12, 0.04, 1), proj='Object')
l.new(tx.outputs['Color'], b.inputs['Base Color'])
body.data.materials.append(walnut)

# Aluminium face, inset from the walnut edge, with the driver's hole cut through.
face = rounded_slab(SIZE - 0.9, 1.4, 0.08, 0.06, 0.02)
bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=3.08, depth=2, location=(0, 0, 0)); hole = bpy.context.active_object
cut = face.modifiers.new('hole', 'BOOLEAN'); cut.operation = 'DIFFERENCE'; cut.object = hole
hole.hide_render = True
cut2 = body.modifiers.new('hole', 'BOOLEAN'); cut2.operation = 'DIFFERENCE'; cut2.object = hole
alu = mat('alu', (0.80, 0.80, 0.79), metallic=1.0, rough=0.30)
n, l, b, tx = image_tex(alu, os.path.join(RENDER, 'brushed.png'), scale=(0.08, 0.5, 1))
ramp = n.new('ShaderNodeMapRange'); ramp.inputs['To Min'].default_value = 0.22; ramp.inputs['To Max'].default_value = 0.40
l.new(tx.outputs['Color'], ramp.inputs['Value']); l.new(ramp.outputs['Result'], b.inputs['Roughness'])
face.data.materials.append(alu)

# --- The driver.
R = 3.05
chrome = mat('chrome', (0.9, 0.9, 0.9), metallic=1.0, rough=0.12)
bpy.ops.mesh.primitive_torus_add(major_segments=160, minor_segments=24, major_radius=R, minor_radius=0.16, location=(0, 0, 0.12)); ring = bpy.context.active_object
ring.scale = (1, 1, 0.55); ring.data.materials.append(chrome)
# The dark well the cone sits in.
bpy.ops.mesh.primitive_cylinder_add(radius=R - 0.05, depth=0.1, location=(0, 0, -0.78)); well = bpy.context.active_object
well.data.materials.append(mat('well', (0.01, 0.01, 0.01), rough=0.8))
# Screws round the ring.
screwm = mat('screw', (0.75, 0.74, 0.72), metallic=1.0, rough=0.25)
for i in range(8):
    a = math.radians(22.5 + 45 * i)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.13, location=((R + 0.42) * math.cos(a), (R + 0.42) * math.sin(a), 0.07))
    s = bpy.context.active_object; s.scale = (1, 1, 0.45); s.data.materials.append(screwm)
# Rubber surround.
rubber = mat('rubber', (0.025, 0.024, 0.023), rough=0.55)
bpy.ops.mesh.primitive_torus_add(major_segments=160, minor_segments=24, major_radius=R - 0.38, minor_radius=0.26, location=(0, 0, 0.05)); sur = bpy.context.active_object
sur.scale = (1, 1, 0.8); sur.data.materials.append(rubber)
# The cone: a shallow funnel, paper with a fine fibre texture and the print.
CR = R - 0.6
bpy.ops.mesh.primitive_cone_add(vertices=128, radius1=0.9, radius2=CR, depth=0.75, end_fill_type='NOTHING', location=(0, 0, -0.33))
cone = bpy.context.active_object
bpy.ops.object.shade_smooth()
paper = mat('paper', (0.035, 0.033, 0.032), rough=0.7)
n = paper.node_tree.nodes; l = paper.node_tree.links; b = n['Principled BSDF']
tc = n.new('ShaderNodeTexCoord'); mp = n.new('ShaderNodeMapping'); tx = n.new('ShaderNodeTexImage')
tx.image = bpy.data.images.load(PRINT); tx.extension = 'CLIP'
# Top-down planar projection: object x,y in [-CR, CR] → 0 … 1.
mp.inputs['Scale'].default_value = (0.5 / CR, 0.5 / CR, 1); mp.inputs['Location'].default_value = (0.5, 0.5, 0)
l.new(tc.outputs['Object'], mp.inputs['Vector']); l.new(mp.outputs['Vector'], tx.inputs['Vector'])
ink = n.new('ShaderNodeMix'); ink.data_type = 'RGBA'
ink.inputs[6].default_value = (0.035, 0.033, 0.032, 1); ink.inputs[7].default_value = (0.82, 0.80, 0.76, 1)
l.new(tx.outputs['Alpha'], ink.inputs[0]); l.new(ink.outputs[2], b.inputs['Base Color'])
inkr = n.new('ShaderNodeMapRange'); inkr.inputs['To Min'].default_value = 0.72; inkr.inputs['To Max'].default_value = 0.35
l.new(tx.outputs['Alpha'], inkr.inputs['Value']); l.new(inkr.outputs['Result'], b.inputs['Roughness'])
nz = n.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = 90; nz.inputs['Detail'].default_value = 10
bump = n.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.12
l.new(nz.outputs['Fac'], bump.inputs['Height']); l.new(bump.outputs['Normal'], b.inputs['Normal'])
cone.data.materials.append(paper)
# Dust cap: spun aluminium dome.
bpy.ops.mesh.primitive_uv_sphere_add(segments=96, ring_count=48, radius=1.0, location=(0, 0, -0.62)); cap = bpy.context.active_object
cap.scale = (1, 1, 0.42); bpy.ops.object.shade_smooth()
capm = mat('cap', (0.85, 0.84, 0.82), metallic=1.0, rough=0.22); spun(capm, 0.9)
cap.data.materials.append(capm)

# A soft shadow under the block, on a catcher.
bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, -1.2)); bpy.context.active_object.is_shadow_catcher = True

path = os.path.join(OUT, 'dss1_icon.png')
sc.render.filepath = path
bpy.ops.render.render(write_still=True)
print('rendered', path)

# The catcher's shadow reaches the frame's edges: fade it out before them so no square shows in the Dock.
img = bpy.data.images.load(path)
px = list(img.pixels); w, h = img.size
base = min(px[(y * w + x) * 4 + 3] for x, y in ((1, h - 2), (w - 2, h - 2)))
margin = 0.09
for y in range(h):
    fy = min(y, h - 1 - y) / (h * margin)
    for x in range(w):
        i = (y * w + x) * 4 + 3
        a = px[i]
        if a >= 0.999:
            continue
        a = max(0.0, (a - base) / (1.0 - base)) if base > 0.002 else a
        f = min(1.0, fy, min(x, w - 1 - x) / (w * margin))
        px[i] = a * f * f * (3 - 2 * f) * 0.8
img.pixels[:] = px
img.save()
print('faded shadow edges')
