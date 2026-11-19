# Seamless surface textures for DSS1's panels: brushed aluminium (faces) and walnut (cheeks).
#   python3 tools/render/make_textures.py assets/render
import sys, numpy as np
from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else 'assets/render'
rng = np.random.default_rng(1977)

def filtered_noise(h, w, sy, sx):
    # Gaussian-filtered noise that wraps at the edges (filtered in frequency space), so it tiles.
    n = rng.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]; fx = np.fft.fftfreq(w)[None, :]
    k = np.exp(-(fy * sy) ** 2 * 2 * np.pi ** 2 - (fx * sx) ** 2 * 2 * np.pi ** 2)
    r = np.real(np.fft.ifft2(np.fft.fft2(n) * k))
    return (r - r.mean()) / (r.std() + 1e-9)

# Brushed aluminium: long horizontal strokes over finer ones, a little cloudiness.
H, W = 768, 1536
strokes = 0.55 * filtered_noise(H, W, 0.6, 180) + 0.3 * filtered_noise(H, W, 0.4, 40) + 0.15 * filtered_noise(H, W, 0.5, 6)
cloud = filtered_noise(H, W, 120, 300)
v = 0.62 + 0.035 * strokes + 0.018 * cloud
rgb = np.stack([v * 0.985, v * 0.99, v * 1.0], axis=-1)
Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)).save(f'{out}/brushed.png', optimize=True)

# Walnut, straight-grained down the side boards: bands across, warped by slow noise, with pores and figure.
# Tall enough that one board down the side of the window doesn't repeat.
H, W = 2400, 240
y = np.arange(H)[:, None]; x = np.arange(W)[None, :]
warp = 4.0 * filtered_noise(H, W, 160, 30) + 1.2 * filtered_noise(H, W, 40, 8)
bands = np.sin((x + warp) * 2 * np.pi / 23.0) * 0.5 + 0.5
fine = np.sin((x + warp * 1.3) * 2 * np.pi / 5.7) * 0.5 + 0.5
pores = np.clip(filtered_noise(H, W, 14, 0.7), 1.6, 4) - 1.6           # dark streaks along the grain
figure = filtered_noise(H, W, 60, 12)
t = np.clip(0.55 * bands + 0.2 * fine + 0.12 * figure, 0, 1)
dark = np.array([0.10, 0.045, 0.022]); light = np.array([0.36, 0.19, 0.09])
col = dark[None, None, :] * (1 - t[..., None]) + light[None, None, :] * t[..., None]
col *= (1 - 0.5 * pores[..., None])
col *= 1.0 + 0.08 * filtered_noise(H, W, 200, 40)[..., None]
Image.fromarray((np.clip(col, 0, 1) * 255).astype(np.uint8)).save(f'{out}/walnut.png', optimize=True)
# Hairline scratches for the faces: mostly along the brushing, a few across; light, with a dark edge. Tiles.
from PIL import ImageDraw
W = H = 1024
img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
for i in range(260):
    x, y = rng.uniform(0, W), rng.uniform(0, H)
    along = rng.random() < 0.8
    ang = rng.normal(0, 0.06) if along else rng.uniform(0, np.pi)
    length = rng.uniform(8, 90) if along else rng.uniform(4, 30)
    a = int(rng.uniform(25, 85))
    for ox, oy in ((0, 0), (-W, 0), (0, -H), (-W, -H), (W, 0), (0, H)):   # wrap, so it tiles
        x2, y2 = x + ox + np.cos(ang) * length, y + oy + np.sin(ang) * length
        d.line([(x + ox, y + oy + 1), (x2, y2 + 1)], fill=(0, 0, 0, a // 2), width=1)
        d.line([(x + ox, y + oy), (x2, y2)], fill=(255, 255, 255, a), width=1)
img.save(f'{out}/scratches.png', optimize=True)

# Wear for keys: a fingertip polish, scuffs and short scratches, placed differently on every key by the app.
W, H = 512, 256
img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
px = np.zeros((H, W, 4), dtype=np.float32)
yy, xx = np.mgrid[0:H, 0:W]
for i in range(10):
    cx, cy, r = rng.uniform(0, W), rng.uniform(0, H), rng.uniform(14, 34)
    blob = np.exp(-(((xx - cx) / r) ** 2 + ((yy - cy) / (r * 0.7)) ** 2))
    px[..., :3] = 255
    px[..., 3] = np.maximum(px[..., 3], blob * rng.uniform(14, 26))
img = Image.fromarray(px.clip(0, 255).astype(np.uint8))
d = ImageDraw.Draw(img)
for i in range(70):
    x, y = rng.uniform(0, W), rng.uniform(0, H)
    ang = rng.uniform(0, np.pi); length = rng.uniform(3, 18)
    d.line([(x, y), (x + np.cos(ang) * length, y + np.sin(ang) * length)], fill=(255, 255, 255, int(rng.uniform(30, 80))), width=1)
img.save(f'{out}/keywear.png', optimize=True)
print('ok')
