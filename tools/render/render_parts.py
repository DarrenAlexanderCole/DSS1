# Renders DSS1's hardware parts with Blender (Cycles), each on a transparent background with its own contact
# shadow, all under one light rig so they match: knobs, keys, toggle switch, screw, pads.
#
#   uv venv /tmp/bvenv -p 3.13 && VIRTUAL_ENV=/tmp/bvenv uv pip install bpy
#   /tmp/bvenv/bin/python tools/render/render_parts.py assets/render [part ...]
#
# Knobs are rendered without a pointer (they look the same at any angle under fixed light); the app draws the
# pointer. Keys are rendered once per style and state and drawn nine-sliced at any size.
import bpy, math, sys, os
from mathutils import Vector

OUT = sys.argv[sys.argv.index('--') + 1] if '--' in sys.argv else (sys.argv[1] if len(sys.argv) > 1 else 'assets/render')
ONLY = sys.argv[sys.argv.index('--') + 2:] if '--' in sys.argv else sys.argv[2:]
os.makedirs(OUT, exist_ok=True)

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = 96
    sc.cycles.use_denoising = True
    sc.render.film_transparent = True
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Medium High Contrast'
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    world = bpy.data.worlds.new('W'); sc.world = world; world.use_nodes = True
    bg = world.node_tree.nodes['Background']
    bg.inputs['Color'].default_value = (0.32, 0.31, 0.30, 1)   # soft studio fill for reflections
    bg.inputs['Strength'].default_value = 0.35
    # Key light high left (matches the brushed sheen drawn on the faceplates), cool fill right, soft top.
    def area(loc, energy, size, color=(1, 1, 1)):
        bpy.ops.object.light_add(type='AREA', location=loc)
        lt = bpy.context.active_object; lt.data.energy = energy; lt.data.size = size; lt.data.color = color
        lt.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    area((-6, -6, 9), 900, 5, (1.0, 0.93, 0.84))
    area((7, -3, 4), 220, 4, (0.78, 0.86, 1.0))
    area((0, 2, 10), 160, 8)
    # The faceplate the part sits on: catches shadows only.
    bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, 0))
    bpy.context.active_object.is_shadow_catcher = True
    return sc

def camera(sc, width_units, px_w, px_h):
    bpy.ops.object.camera_add(location=(0, 0, 20), rotation=(0, 0, 0))
    cam = bpy.context.active_object; cam.data.type = 'ORTHO'
    cam.data.ortho_scale = width_units
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = px_w, px_h

def render(sc, name):
    path = os.path.join(OUT, name + '.png')
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    # The shadow catcher dims the whole frame a little (the world's light is partly blocked everywhere): take
    # that baseline off, so only the part's own shadow is left.
    img = bpy.data.images.load(path)
    px = list(img.pixels)
    w, h = img.size
    corners = [px[(y * w + x) * 4 + 3] for x, y in ((1, 1), (w - 2, 1), (1, h - 2), (w - 2, h - 2))]
    base = min(corners)
    margin = 0.14
    for y in range(h):
        fy = min(y, h - 1 - y) / (h * margin)
        for x in range(w):
            i = (y * w + x) * 4 + 3
            a = px[i]
            if a >= 0.999:
                continue
            a = max(0.0, (a - base) / (1.0 - base)) if base > 0.002 else a
            # Shadows fade out before the frame edge, so nothing shows a square outline.
            f = min(1.0, fy, min(x, w - 1 - x) / (w * margin))
            px[i] = a * f * f * (3 - 2 * f)
    img.pixels[:] = px
    img.save()
    print('rendered', name)

# --- Materials ----------------------------------------------------------------------------------------------
def principled(name, color, metallic=0.0, rough=0.4, coat=0.0, aniso=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*color, 1)
    b.inputs['Metallic'].default_value = metallic
    b.inputs['Roughness'].default_value = rough
    b.inputs['Coat Weight'].default_value = coat
    b.inputs['Anisotropic'].default_value = aniso
    return m

def add_bump(m, build, strength):
    n = m.node_tree.nodes; l = m.node_tree.links; b = n['Principled BSDF']
    out = build(n, l)
    bump = n.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = strength
    l.new(out, bump.inputs['Height'])
    prev = [lk for lk in l if lk.to_socket == b.inputs['Normal']]
    if prev:
        l.new(prev[0].from_socket, bump.inputs['Normal'])
    l.new(bump.outputs['Normal'], b.inputs['Normal'])

def rings(scale):
    # Concentric machining (lathe rings) round the Z axis.
    def build(n, l):
        tc = n.new('ShaderNodeTexCoord'); w = n.new('ShaderNodeTexWave')
        w.wave_type = 'RINGS'; w.rings_direction = 'Z'; w.inputs['Scale'].default_value = scale; w.inputs['Detail'].default_value = 0
        l.new(tc.outputs['Object'], w.inputs['Vector']); return w.outputs['Fac']
    return build

def knurl(count):
    # Grip ridges by angle round Z.
    def build(n, l):
        tc = n.new('ShaderNodeTexCoord'); sep = n.new('ShaderNodeSeparateXYZ')
        at = n.new('ShaderNodeMath'); at.operation = 'ARCTAN2'
        mul = n.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'; mul.inputs[1].default_value = count
        sn = n.new('ShaderNodeMath'); sn.operation = 'SINE'
        l.new(tc.outputs['Object'], sep.inputs['Vector']); l.new(sep.outputs['Y'], at.inputs[0]); l.new(sep.outputs['X'], at.inputs[1])
        l.new(at.outputs['Value'], mul.inputs[0]); l.new(mul.outputs['Value'], sn.inputs[0]); return sn.outputs['Value']
    return build

def grain(scale):
    def build(n, l):
        nz = n.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = scale; nz.inputs['Detail'].default_value = 8
        return nz.outputs['Fac']
    return build

def spun(m, axis='Z', amount=0.9):
    # Spun / brushed metal: anisotropic with radial (spun) or straight (brushed) tangents.
    n = m.node_tree.nodes; l = m.node_tree.links; b = n['Principled BSDF']
    t = n.new('ShaderNodeTangent')
    if axis == 'radial':
        t.direction_type = 'RADIAL'; t.axis = 'Z'
    else:
        t.direction_type = 'RADIAL'; t.axis = axis
    b.inputs['Anisotropic'].default_value = amount
    l.new(t.outputs['Tangent'], b.inputs['Tangent'])

def emission(name, color, strength):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nn = m.node_tree.nodes; ll = m.node_tree.links
    for x in list(nn): nn.remove(x)
    o = nn.new('ShaderNodeOutputMaterial'); e = nn.new('ShaderNodeEmission')
    e.inputs['Color'].default_value = (*color, 1); e.inputs['Strength'].default_value = strength
    ll.new(e.outputs['Emission'], o.inputs['Surface']); return m

# --- Shapes -------------------------------------------------------------------------------------------------
def cylinder(r, h, z, m, verts=128, bevel=0.0, segs=4, name='c'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=h, location=(0, 0, z + h / 2))
    o = bpy.context.active_object; o.name = name
    if bevel:
        b = o.modifiers.new('b', 'BEVEL'); b.width = bevel; b.segments = segs; b.limit_method = 'ANGLE'
    bpy.ops.object.shade_smooth()
    o.data.materials.append(m); return o

def cone(r1, r2, h, z, m, verts=128, bevel=0.0, name='k'):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r1, radius2=r2, depth=h, location=(0, 0, z + h / 2))
    o = bpy.context.active_object; o.name = name
    if bevel:
        b = o.modifiers.new('b', 'BEVEL'); b.width = bevel; b.segments = 4; b.limit_method = 'ANGLE'
    bpy.ops.object.shade_smooth()
    o.data.materials.append(m); return o

def rounded_box(w, d, h, z, radius, m, name='box'):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, z + h / 2))
    o = bpy.context.active_object; o.name = name; o.scale = (w, d, h)
    bpy.ops.object.transform_apply(scale=True)
    b = o.modifiers.new('b', 'BEVEL'); b.width = radius; b.segments = 8; b.limit_method = 'NONE'
    bpy.ops.object.shade_smooth()
    o.data.materials.append(m); return o

# --- Parts --------------------------------------------------------------------------------------------------
def knob(name, cap_color, cap_metal, skirt=True):
    # A machined knob: knurled body, lathe-ringed cap, a thin dark skirt on the panel. 1 unit = knob radius.
    sc = reset()
    body = principled('Body', (0.78, 0.78, 0.79), 1.0, 0.26)
    add_bump(body, knurl(64), 0.5)
    cap = principled('Cap', cap_color, cap_metal, 0.30)
    spun(cap, 'radial', 0.85)
    add_bump(cap, rings(140), 0.04)
    dark = principled('Skirt', (0.015, 0.015, 0.016), 0.4, 0.35)
    if skirt:
        cone(1.10, 1.04, 0.10, 0.0, dark, bevel=0.03, name='skirt')
    cone(1.0, 0.86, 0.9, 0.12, body, bevel=0.03, name='body')         # tapered, so the knurled side shows
    cone(0.86, 0.80, 0.06, 1.02, body, name='chamfer')
    cylinder(0.80, 0.04, 1.08, cap, bevel=0.015, name='cap')
    camera(sc, 3.1, 320, 320)
    render(sc, name)

def worn_metal(name, base, rough):
    # Brushed, anodised metal that's been used and looked after: bare aluminium showing through where the edges
    # are rubbed, a satin polish in the middle where fingers land, faint scratches along the brushing.
    m = principled(name, base, 0.9, rough)
    n = m.node_tree.nodes; l = m.node_tree.links; b = n['Principled BSDF']
    spun(m, 'X', 0.8)
    geo = n.new('ShaderNodeNewGeometry'); bev = n.new('ShaderNodeBevel'); bev.inputs['Radius'].default_value = 0.05
    dot = n.new('ShaderNodeVectorMath'); dot.operation = 'DOT_PRODUCT'
    l.new(bev.outputs['Normal'], dot.inputs[0]); l.new(geo.outputs['Normal'], dot.inputs[1])
    edge = n.new('ShaderNodeMapRange'); edge.inputs['From Min'].default_value = 0.999; edge.inputs['From Max'].default_value = 0.93
    l.new(dot.outputs['Value'], edge.inputs['Value'])
    patch = n.new('ShaderNodeTexNoise'); patch.inputs['Scale'].default_value = 9; patch.inputs['Detail'].default_value = 6
    mul = n.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'
    pr = n.new('ShaderNodeMapRange'); pr.inputs['From Min'].default_value = 0.35; pr.inputs['From Max'].default_value = 0.65
    l.new(patch.outputs['Fac'], pr.inputs['Value'])
    l.new(edge.outputs['Result'], mul.inputs[0]); l.new(pr.outputs['Result'], mul.inputs[1])
    mix = n.new('ShaderNodeMix'); mix.data_type = 'RGBA'
    mix.inputs['A'].default_value = (*base, 1); mix.inputs['B'].default_value = (0.78, 0.78, 0.78, 1)
    l.new(mul.outputs['Value'], mix.inputs['Factor']); l.new(mix.outputs['Result'], b.inputs['Base Color'])
    # Polish: smoother towards the middle of the top, uneven.
    tc = n.new('ShaderNodeTexCoord'); grad = n.new('ShaderNodeTexGradient'); grad.gradient_type = 'SPHERICAL'
    mp = n.new('ShaderNodeMapping'); mp.inputs['Scale'].default_value = (0.75, 1.8, 1)
    l.new(tc.outputs['Object'], mp.inputs['Vector']); l.new(mp.outputs['Vector'], grad.inputs['Vector'])
    smudge = n.new('ShaderNodeTexNoise'); smudge.inputs['Scale'].default_value = 4
    sm = n.new('ShaderNodeMath'); sm.operation = 'MULTIPLY'
    l.new(grad.outputs['Fac'], sm.inputs[0]); l.new(smudge.outputs['Fac'], sm.inputs[1])
    rr = n.new('ShaderNodeMapRange'); rr.inputs['To Min'].default_value = rough; rr.inputs['To Max'].default_value = rough * 0.45
    l.new(sm.outputs['Value'], rr.inputs['Value'])
    # Scratches: fine streaks along the brushing.
    sc = n.new('ShaderNodeTexNoise'); sc.inputs['Scale'].default_value = 30; sc.inputs['Detail'].default_value = 2
    sm2 = n.new('ShaderNodeMapping'); sm2.inputs['Scale'].default_value = (0.08, 6, 1)
    l.new(tc.outputs['Object'], sm2.inputs['Vector']); l.new(sm2.outputs['Vector'], sc.inputs['Vector'])
    st = n.new('ShaderNodeMapRange'); st.inputs['From Min'].default_value = 0.68; st.inputs['From Max'].default_value = 0.74
    st.inputs['To Min'].default_value = 0.0; st.inputs['To Max'].default_value = -0.12
    l.new(sc.outputs['Fac'], st.inputs['Value'])
    add = n.new('ShaderNodeMath'); add.operation = 'ADD'; add.use_clamp = True
    l.new(rr.outputs['Result'], add.inputs[0]); l.new(st.outputs['Result'], add.inputs[1])
    l.new(add.outputs['Value'], b.inputs['Roughness'])
    return m

def key(name, color, metal, rough, coat=0.0, emit=None, down=False, worn=False):
    # A square-cornered hardware key (radius small) on its panel cut-out. 1 unit = key height.
    sc = reset()
    if worn:
        top = worn_metal('Cap', color, rough)
    elif emit:
        top = principled('Cap', color, metal, rough, coat)
        b = top.node_tree.nodes['Principled BSDF']
        b.inputs['Emission Color'].default_value = (*emit[0], 1)
        b.inputs['Emission Strength'].default_value = emit[1]
    else:
        top = principled('Cap', color, metal, rough, coat)
    if metal > 0.5 and not worn:
        spun(top, 'X', 0.8)                                                  # brushed across
    add_bump(top, grain(180), 0.03)
    well = principled('Well', (0.01, 0.01, 0.01), 0.3, 0.5)
    rounded_box(2.62, 1.02, 0.08, 0.0, 0.035, well, name='well')            # the dark cut-out round the key
    h = 0.16 if down else 0.34
    rounded_box(2.5, 0.9, h, 0.0, 0.07, top, name='cap')
    camera(sc, 2.9, 320, 128)
    render(sc, name)

def toggle(name, up):
    # A chrome bat-handle toggle on a hex nut.
    sc = reset()
    chrome = principled('Chrome', (0.9, 0.9, 0.9), 1.0, 0.08)
    nut = principled('Nut', (0.78, 0.78, 0.79), 1.0, 0.25)
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.55, depth=0.18, location=(0, 0, 0.09))
    o = bpy.context.active_object; b = o.modifiers.new('b', 'BEVEL'); b.width = 0.03; b.segments = 3; o.data.materials.append(nut)
    cylinder(0.32, 0.22, 0.18, chrome, verts=64, bevel=0.03, name='bush')
    # the lever: a tapered rod tipped forward (towards the viewer's top or bottom)
    bpy.ops.mesh.primitive_cone_add(vertices=48, radius1=0.09, radius2=0.14, depth=1.3, location=(0, 0, 0))
    lever = bpy.context.active_object; bpy.ops.object.shade_smooth(); lever.data.materials.append(chrome)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.16, location=(0, 0, 0.65))
    ball = bpy.context.active_object; bpy.ops.object.shade_smooth(); ball.data.materials.append(chrome)
    ball.parent = lever
    lever.location = (0, 0.42 if up else -0.42, 0.75)
    lever.rotation_euler = (math.radians(-38 if up else 38), 0, 0)
    camera(sc, 1.9, 160, 160)
    render(sc, name)

def screw(name):
    sc = reset()
    steel = principled('Steel', (0.72, 0.72, 0.72), 1.0, 0.3)
    add_bump(steel, grain(60), 0.05)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=1.0, location=(0, 0, -0.62))
    head = bpy.context.active_object; bpy.ops.object.shade_smooth(); head.data.materials.append(steel)
    head.scale = (1, 1, 1)
    # cut a Phillips cross (two thin boxes, boolean difference)
    for a in (0, 90):
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0.35))
        cut = bpy.context.active_object; cut.scale = (1.3, 0.18, 0.5); cut.rotation_euler = (0, 0, math.radians(a + 20))
        mod = head.modifiers.new('cut%d' % a, 'BOOLEAN'); mod.object = cut; mod.operation = 'DIFFERENCE'
        cut.hide_render = True
    camera(sc, 2.6, 96, 96)
    render(sc, name)

def pad(name, down=False):
    # An MPC-style silicone pad: soft rounded square, slightly translucent.
    sc = reset()
    rubber = principled('Rubber', (0.20, 0.20, 0.21), 0.0, 0.62)
    b = rubber.node_tree.nodes['Principled BSDF']
    b.inputs['Subsurface Weight'].default_value = 0.25
    add_bump(rubber, grain(40), 0.08)
    well = principled('Well', (0.008, 0.008, 0.008), 0.0, 0.6)
    rounded_box(2.12, 2.12, 0.06, 0.0, 0.08, well, name='well')
    rounded_box(2.0, 2.0, 0.12 if down else 0.24, 0.0, 0.11, rubber, name='pad')
    camera(sc, 2.4, 256, 256)
    render(sc, name)

def power_lamp(name):
    # A pilot lamp: a faceted red jewel in a knurled chrome bezel.
    sc = reset()
    chrome = principled('Chrome', (0.9, 0.9, 0.9), 1.0, 0.1)
    add_bump(chrome, knurl(36), 0.2)
    cylinder(1.0, 0.25, 0.0, chrome, bevel=0.06, name='bezel')
    cone(0.82, 0.72, 0.12, 0.25, chrome, bevel=0.02, name='lip')
    jewel = bpy.data.materials.new('Jewel'); jewel.use_nodes = True
    b = jewel.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (0.55, 0.01, 0.005, 1)
    b.inputs['Roughness'].default_value = 0.05
    b.inputs['Transmission Weight'].default_value = 0.6
    b.inputs['Emission Color'].default_value = (1.0, 0.03, 0.005, 1)
    b.inputs['Emission Strength'].default_value = 0.35
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=0.66, location=(0, 0, 0.3))
    o = bpy.context.active_object; o.scale = (1, 1, 0.7); o.data.materials.append(jewel)   # faceted (flat shaded)
    camera(sc, 2.6, 128, 128)
    render(sc, name)

def bakelite_key(name, down=False):
    # A black phenolic (bakelite) key: satin, hard, a little worn at the edges and polished where fingers land.
    sc = reset()
    top = principled('Cap', (0.006, 0.006, 0.007), 0.0, 0.5, coat=0.7)
    n = top.node_tree.nodes; l = top.node_tree.links; bs = n['Principled BSDF']
    bs.inputs['Coat Roughness'].default_value = 0.14
    # Edges rubbed a touch lighter (worn coat); centre a little glossier.
    geo = n.new('ShaderNodeNewGeometry'); bev = n.new('ShaderNodeBevel'); bev.inputs['Radius'].default_value = 0.05
    dot = n.new('ShaderNodeVectorMath'); dot.operation = 'DOT_PRODUCT'
    l.new(bev.outputs['Normal'], dot.inputs[0]); l.new(geo.outputs['Normal'], dot.inputs[1])
    edge = n.new('ShaderNodeMapRange'); edge.inputs['From Min'].default_value = 0.999; edge.inputs['From Max'].default_value = 0.9
    l.new(dot.outputs['Value'], edge.inputs['Value'])
    mix = n.new('ShaderNodeMix'); mix.data_type = 'RGBA'
    mix.inputs['A'].default_value = (0.006, 0.006, 0.007, 1); mix.inputs['B'].default_value = (0.06, 0.056, 0.052, 1)
    l.new(edge.outputs['Result'], mix.inputs['Factor']); l.new(mix.outputs['Result'], bs.inputs['Base Color'])
    tc = n.new('ShaderNodeTexCoord'); grad = n.new('ShaderNodeTexGradient'); grad.gradient_type = 'SPHERICAL'
    mp = n.new('ShaderNodeMapping'); mp.inputs['Scale'].default_value = (0.75, 1.8, 1)
    l.new(tc.outputs['Object'], mp.inputs['Vector']); l.new(mp.outputs['Vector'], grad.inputs['Vector'])
    rr = n.new('ShaderNodeMapRange'); rr.inputs['To Min'].default_value = 0.5; rr.inputs['To Max'].default_value = 0.3
    l.new(grad.outputs['Fac'], rr.inputs['Value']); l.new(rr.outputs['Result'], bs.inputs['Roughness'])
    add_bump(top, grain(220), 0.02)
    well = principled('Well', (0.01, 0.01, 0.01), 0.3, 0.5)
    rounded_box(2.62, 1.02, 0.08, 0.0, 0.035, well, name='well')
    h = 0.16 if down else 0.34
    rounded_box(2.5, 0.9, h, 0.0, 0.13, top, name='cap')
    camera(sc, 2.9, 320, 128)
    render(sc, name)

def rawhide_pad(name, down=False):
    # A drum pad: treated rawhide, dyed near-black, stretched over a square frame (texture from make_textures.py:
    # fibres, mottling, a polished wear groove where it's played), in a dark metal bezel with a light-pipe ring.
    sc = reset()
    hide = bpy.data.materials.new('Hide'); hide.use_nodes = True
    n = hide.node_tree.nodes; l = hide.node_tree.links; bs = n['Principled BSDF']
    tex = n.new('ShaderNodeTexImage')
    tex.image = bpy.data.images.load(os.path.abspath(os.path.join(OUT, 'rawhide.png')))
    tc = n.new('ShaderNodeTexCoord'); mp = n.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (0.5, 0.5, 1); mp.inputs['Location'].default_value = (0.5, 0.5, 0)
    l.new(tc.outputs['Object'], mp.inputs['Vector']); l.new(mp.outputs['Vector'], tex.inputs['Vector'])
    # Dyed dark charcoal: the hide's fibres and mottling stay, just deep in the dye.
    dye = n.new('ShaderNodeHueSaturation')
    dye.inputs['Saturation'].default_value = 0.10; dye.inputs['Value'].default_value = 0.11
    l.new(tex.outputs['Color'], dye.inputs['Color']); l.new(dye.outputs['Color'], bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value = 0.6
    bs.inputs['Coat Weight'].default_value = 0.3; bs.inputs['Coat Roughness'].default_value = 0.4   # treated, oiled
    bs.inputs['Subsurface Weight'].default_value = 0.05
    bump = n.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.45
    l.new(tex.outputs['Alpha'], bump.inputs['Height']); l.new(bump.outputs['Normal'], bs.inputs['Normal'])
    bezel = principled('Bezel', (0.03, 0.03, 0.032), 0.8, 0.35)
    ring = principled('Ring', (0.75, 0.75, 0.72), 0.0, 0.25)          # frosted light pipe (lit by the app)
    ring.node_tree.nodes['Principled BSDF'].inputs['Transmission Weight'].default_value = 0.5
    rounded_box(2.16, 2.16, 0.10, 0.0, 0.12, bezel, name='bezel')
    rounded_box(2.04, 2.04, 0.13, 0.0, 0.11, ring, name='ring')
    rounded_box(1.92, 1.92, 0.12, 0.0, 0.10, bezel, name='seat')
    # The hide: a thin slab domed a little (stretched), pressed down when played.
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=64, y_subdivisions=64, size=1.86, location=(0, 0, 0.16))
    o = bpy.context.active_object
    for v in o.data.vertices:
        x, y = v.co.x / 0.93, v.co.y / 0.93
        edge = max(abs(x), abs(y))
        v.co.z = (0.05 if not down else -0.01) * (1 - edge ** 6)
    sol = o.modifiers.new('s', 'SOLIDIFY'); sol.thickness = 0.04
    bpy.ops.object.shade_smooth()
    o.data.materials.append(hide)
    camera(sc, 2.4, 256, 256)
    render(sc, name)

PARTS = {
    'power_lamp': lambda: power_lamp('power_lamp'),
    'key_black_up': lambda: bakelite_key('key_black_up'),
    'key_black_down': lambda: bakelite_key('key_black_down', True),
    'pad_hide_up': lambda: rawhide_pad('pad_hide_up'),
    'pad_hide_down': lambda: rawhide_pad('pad_hide_down', True),
    'knob_alu': lambda: knob('knob_alu', (0.84, 0.84, 0.85), 1.0),
    'knob_brass': lambda: knob('knob_brass', (0.88, 0.62, 0.30), 1.0),
    'key_dark_up': lambda: key('key_dark_up', (0.16, 0.165, 0.175), 0.9, 0.34, worn=True),
    'key_dark_down': lambda: key('key_dark_down', (0.16, 0.165, 0.175), 0.9, 0.34, down=True, worn=True),
    'key_cream_up': lambda: key('key_cream_up', (0.80, 0.74, 0.58), 0.0, 0.38, coat=0.4),
    'key_cream_down': lambda: key('key_cream_down', (0.80, 0.74, 0.58), 0.0, 0.38, coat=0.4, down=True),
    'key_cue_up': lambda: key('key_cue_up', (0.85, 0.17, 0.01), 0.0, 0.3, coat=0.6, emit=((1.0, 0.25, 0.02), 0.12)),
    'key_cue_down': lambda: key('key_cue_down', (0.85, 0.17, 0.01), 0.0, 0.3, coat=0.6, emit=((1.0, 0.25, 0.02), 0.3), down=True),
    'key_rec_up': lambda: key('key_rec_up', (0.55, 0.06, 0.04), 0.0, 0.3, coat=0.6),
    'key_rec_down': lambda: key('key_rec_down', (1.0, 0.12, 0.06), 0.0, 0.3, coat=0.6, emit=((1.0, 0.1, 0.03), 1.5), down=True),
    'key_alu_up': lambda: key('key_alu_up', (0.82, 0.82, 0.83), 1.0, 0.26),
    'key_alu_down': lambda: key('key_alu_down', (0.82, 0.82, 0.83), 1.0, 0.26, down=True),
    'toggle_up': lambda: toggle('toggle_up', True),
    'toggle_down': lambda: toggle('toggle_down', False),
    'screw': lambda: screw('screw'),
    'pad_up': lambda: pad('pad_up'),
    'pad_down': lambda: pad('pad_down', True),
}
for name, fn in PARTS.items():
    if not ONLY or name in ONLY:
        fn()
