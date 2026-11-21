# Vertical (9:16, 1080 x 1920) beta promo cards, one per screen: the same design as the 16:9 Facebook post.
#
#   python3 tools/guide/make_promo_9x16.py <shots folder (full-size PNGs)> <out folder>
import os, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHOTS = sys.argv[1] if len(sys.argv) > 1 else '/tmp/shots'
OUT = sys.argv[2] if len(sys.argv) > 2 else '/tmp/promo916'
os.makedirs(OUT, exist_ok=True)
F = lambda n, s: ImageFont.truetype(os.path.join(ROOT, 'assets', 'fonts', n), s)
BRAND, BOLD, MED = 'Michroma-Regular.ttf', 'ArchivoNarrow-Bold.ttf', 'ArchivoNarrow-Medium.ttf'
W, H = 1080, 1920
AMBER, CREAM, SOFT, RED, BG = (255, 154, 46), (243, 236, 226), (170, 160, 148), (214, 72, 52), (16, 13, 12)


def card(shot, name):
    img = Image.new('RGB', (W, H), BG)
    glow = Image.new('L', (W, H), 0)
    ImageDraw.Draw(glow).ellipse((-400, 500, W + 400, 1700), fill=85)
    img.paste(Image.new('RGB', (W, H), (95, 48, 14)), (0, 0), glow.filter(ImageFilter.GaussianBlur(200)))
    d = ImageDraw.Draw(img)
    for x0 in (0, W - 18):
        d.rectangle((x0, 0, x0 + 17, H), fill=(42, 26, 16))
    cx = W // 2
    d.rounded_rectangle((cx - 230, 150, cx + 230, 222), 36, fill=RED)
    d.text((cx, 186), 'NOW IN BETA TESTING', font=F(BOLD, 38), fill=CREAM, anchor='mm')
    d.text((cx, 275), 'DSS1', font=F(BRAND, 150), fill=CREAM, anchor='ma')
    d.text((cx, 470), 'DIGITAL SAMPLE STATION  ·  V2', font=F(BOLD, 42), fill=AMBER, anchor='ma')
    # The screen.
    pic = Image.open(os.path.join(SHOTS, shot)).convert('RGB').crop((128, 50, 1472, 1030))
    pw = W - 100
    pic = pic.resize((pw, int(pic.height * pw / pic.width)), Image.LANCZOS)
    x, y = 50, 540
    sh = Image.new('L', (W, H), 0)
    ImageDraw.Draw(sh).rounded_rectangle((x + 10, y + 24, x + pw + 10, y + pic.height + 24), 20, fill=200)
    img.paste((0, 0, 0), (0, 0), sh.filter(ImageFilter.GaussianBlur(28)))
    m = Image.new('L', pic.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, pic.width - 1, pic.height - 1), 20, fill=255)
    img.paste(pic, (x, y), m)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((x, y, x + pw - 1, y + pic.height - 1), 20, outline=(80, 68, 58), width=2)
    yy = y + pic.height + 56
    for line in ['Get your music ready', 'for the show or the session.']:
        d.text((cx, yy), line, font=F(MED, 56), fill=CREAM, anchor='ma')
        yy += 72
    yy += 16
    for line in ['Sort · Prep · Shape · Chop · Deliver', 'Rekordbox · Serato · MPC']:
        d.text((cx, yy), line, font=F(MED, 42), fill=SOFT, anchor='ma')
        yy += 54
    yy += 30
    assert yy + 104 < H - 190, yy
    d.rounded_rectangle((cx - 380, yy, cx + 380, yy + 104), 52, fill=AMBER)
    d.text((cx, yy + 52), 'Free to test. Tell us what you think.', font=F(BOLD, 42), fill=BG, anchor='mm')
    d.text((cx, H - 165), 'github.com/DarrenAlexanderCole/DSS1', font=F(BOLD, 44), fill=CREAM, anchor='ma')
    d.text((cx, H - 100), 'macOS 13+  ·  Apple Silicon & Intel  ·  Free & open source', font=F(MED, 32), fill=SOFT, anchor='ma')
    path = os.path.join(OUT, f'DSS1-V2-Beta-Promo-9x16-{name}.jpg')
    img.save(path, quality=92)
    return path


for shot, name in [('05_chop.png', 'Chop'), ('02_catalog.png', 'Catalog'), ('03_prep.png', 'Prep'),
                   ('04_listen.png', 'Listen-Design'), ('06_deliver.png', 'Deliver'), ('01_cover.png', 'Dust-Cover')]:
    print(card(shot, name))
