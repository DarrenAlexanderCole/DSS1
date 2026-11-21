# Landscape (16:9, 1920 x 1080) versions of the DSS1 V2 feature slides: the same seven slides as
# make_slides.py, with the words on the left and the screen on the right.
#
#   python3 tools/guide/make_slides_16x9.py <shots folder (full-size PNGs)> <out folder>
import os, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHOTS = sys.argv[1] if len(sys.argv) > 1 else '/tmp/shots'
OUT = sys.argv[2] if len(sys.argv) > 2 else '/tmp/slides16x9'
os.makedirs(OUT, exist_ok=True)
F = lambda name, size: ImageFont.truetype(os.path.join(ROOT, 'assets', 'fonts', name), size)
BRAND, BOLD, MED, DISP = 'Michroma-Regular.ttf', 'ArchivoNarrow-Bold.ttf', 'ArchivoNarrow-Medium.ttf', 'RajdhaniTab-SemiBold.ttf'
W, H = 1920, 1080
AMBER, CREAM, SOFT, RED, BG = (255, 154, 46), (243, 236, 226), (170, 160, 148), (214, 72, 52), (16, 13, 12)
L = 90  # left margin


def base():
    img = Image.new('RGB', (W, H), BG)
    glow = Image.new('L', (W, H), 0)
    ImageDraw.Draw(glow).ellipse((700, -500, 2600, 1300), fill=80)
    img.paste(Image.new('RGB', (W, H), (95, 48, 14)), (0, 0), glow.filter(ImageFilter.GaussianBlur(220)))
    d = ImageDraw.Draw(img)
    for y0 in (0, H - 16):
        d.rectangle((0, y0, W, y0 + 15), fill=(42, 26, 16))
    return img


def wrap(d, text, font, width):
    lines, line = [], ''
    for word in text.split():
        test = (line + ' ' + word).strip()
        if d.textlength(test, font=font) <= width:
            line = test
        else:
            lines.append(line)
            line = word
    if line:
        lines.append(line)
    return lines


def crop(name, box=(128, 50, 1472, 1030)):
    return Image.open(os.path.join(SHOTS, name)).convert('RGB').crop(box)


def framed(img, pic, x, y, width, radius=20):
    pic = pic.resize((width, int(pic.height * width / pic.width)), Image.LANCZOS)
    shadow = Image.new('L', (W, H), 0)
    ImageDraw.Draw(shadow).rounded_rectangle((x + 12, y + 26, x + width + 12, y + pic.height + 26), radius, fill=200)
    img.paste((0, 0, 0), (0, 0), shadow.filter(ImageFilter.GaussianBlur(30)))
    mask = Image.new('L', pic.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, pic.width - 1, pic.height - 1), radius, fill=255)
    img.paste(pic, (x, y), mask)
    ImageDraw.Draw(img).rounded_rectangle((x, y, x + width - 1, y + pic.height - 1), radius, outline=(80, 68, 58), width=2)
    return pic.height


def right_shot(img, pic, width=1000):
    h = int(pic.height * width / pic.width)
    framed(img, pic, W - width - 70, (H - h) // 2, width)


def footer(img):
    d = ImageDraw.Draw(img)
    d.text((L, H - 105), 'github.com/DarrenAlexanderCole/DSS1', font=F(MED, 30), fill=SOFT)


def feature(step, kicker, title, items, pic, width=1000):
    img = base()
    right_shot(img, pic, width)
    d = ImageDraw.Draw(img)
    textW = W - width - 70 - L - 70
    d.text((L, 95), 'DSS1', font=F(BRAND, 34), fill=CREAM)
    d.text((L + 170, 100), step, font=F(DISP, 34), fill=AMBER)
    d.text((L, 200), kicker.upper(), font=F(BOLD, 36), fill=RED)
    y = 252
    for line in wrap(d, title, F(BRAND, 54), textW):
        d.text((L, y), line, font=F(BRAND, 54), fill=CREAM)
        y += 72
    y += 40
    size = 36
    for head, text in items:
        d.ellipse((L + 2, y + 15, L + 16, y + 29), fill=AMBER)
        words = (head + ' ' + text).split()
        lines = wrap(d, head + ' ' + text, F(MED, size), textW - 40)
        headWords = len(head.split())
        for i, line in enumerate(lines):
            x = L + 38
            if i == 0:
                # The heading words in amber bold, the rest in cream.
                lw = line.split()
                hpart = ' '.join(lw[:headWords])
                d.text((x, y), hpart, font=F(BOLD, size), fill=AMBER)
                x += d.textlength(hpart + ' ', font=F(BOLD, size))
                d.text((x, y), ' '.join(lw[headWords:]), font=F(MED, size), fill=CREAM)
            else:
                d.text((x, y), line, font=F(MED, size), fill=CREAM)
            y += int(size * 1.32)
        y += 20
    footer(img)
    return img


slides = []

# 1. Title
img = base()
right_shot(img, crop('05_chop.png'))
d = ImageDraw.Draw(img)
d.rounded_rectangle((L, 120, L + 430, 186), 33, fill=RED)
d.text((L + 215, 153), 'V2 BETA  ·  macOS', font=F(BOLD, 34), fill=CREAM, anchor='mm')
d.text((L, 225), 'DSS1', font=F(BRAND, 150), fill=CREAM)
d.text((L + 6, 420), 'DIGITAL SAMPLE STATION', font=F(BOLD, 40), fill=AMBER)
y = 500
for line in ['Get your music ready', 'for the show or the session.']:
    d.text((L, y), line, font=F(MED, 52), fill=CREAM)
    y += 66
y += 24
for line in ['Sort it. Prep it. Shape it.', 'Chop it. Deliver it.']:
    d.text((L, y), line, font=F(MED, 40), fill=SOFT)
    y += 54
footer(img)
slides.append(img)

slides.append(feature('1 / 5', 'Catalog', 'Your music, sorted.', [
    ('Drop in', 'files, folders or record-pool zips.'),
    ('BPM, key, loudness', 'and the beat grid are worked out for you.'),
    ('Notes', 'on every track, and they travel to Rekordbox.'),
    ('Never changed:', 'your music files stay exactly as they are.')], crop('02_catalog.png')))
slides.append(feature('2 / 5', 'Prep', 'Cues, loops and the grid.', [
    ('Hot cues A–H,', 'memory cues and saved loops.'),
    ('Loops resize live', 'as you turn the LOOP knob.'),
    ('Fix the grid:', 'beat 1, nudge, half/double, tempo changes.')], crop('03_prep.png')))
slides.append(feature('3 / 5', 'Listen / Design', 'Shape the sound.', [
    ('ECHO:', 'PING, TAPE, DUCK, FREEZE.'),
    ('REVERB:', 'GATE and HALO shimmer.'),
    ('COMP:', 'opto with EQ, plus DRIVE, PUNCH, SQUASH, CRUSH.'),
    ('Record', 'what you hear, then send it straight to Chop.')], crop('04_listen.png')))
slides.append(feature('4 / 5', 'Chop', 'Chop anything onto 128 pads.', [
    ('Tap an empty pad', 'to mark a slice; tap it again to play it.'),
    ('Each pad:', 'gain, tune, attack, fade, reverse.'),
    ('Keyboard or MIDI', 'pads (Akai MPC, MPD, MPK). 0 stops all.')], crop('05_chop.png')))
slides.append(feature('5 / 5', 'Deliver', 'Send it to your setup.', [
    ('Rekordbox:', 'one XML with cues, loops, grids and notes.'),
    ('Serato:', 'tagged copies, cues and loops inside.'),
    ('Chop banks', 'save as WAVs, ready for your MPC.')], crop('06_deliver.png')))

# 7. Get it
img = base()
d = ImageDraw.Draw(img)
icon = Image.open(os.path.join(ROOT, 'assets', 'icon', 'dss1_icon.png')).convert('RGBA').resize((520, 520), Image.LANCZOS)
img.paste(icon, (W - 520 - 220, (H - 520) // 2 - 20), icon)
d.text((L, 150), 'Try the beta', font=F(BRAND, 76), fill=CREAM)
y = 300
for n, text in [('1', 'Download DSS1 from the Releases page on GitHub.'),
                ('2', 'Drag DSS1 into Applications.'),
                ('3', 'First open: System Settings → Privacy & Security → Open Anyway.')]:
    d.ellipse((L, y, L + 60, y + 60), fill=AMBER)
    d.text((L + 30, y + 30), n, font=F(DISP, 44), fill=BG, anchor='mm')
    yy = y + 4
    for line in wrap(d, text, F(MED, 44), 900):
        d.text((L + 90, yy), line, font=F(MED, 44), fill=CREAM)
        yy += 58
    y = max(y + 100, yy + 36)
d.rounded_rectangle((L, y + 30, L + 820, y + 170), 30, outline=AMBER, width=3)
d.text((L + 410, y + 68), 'github.com/', font=F(MED, 40), fill=SOFT, anchor='ma')
d.text((L + 410, y + 112), 'DarrenAlexanderCole/DSS1', font=F(BOLD, 46), fill=AMBER, anchor='ma')
d.text((L, H - 105), 'macOS 13+  ·  Apple Silicon & Intel  ·  Free & open source', font=F(MED, 32), fill=SOFT)
slides.append(img)

for i, s in enumerate(slides, 1):
    s.save(os.path.join(OUT, f'DSS1-slide-16x9-{i:02d}.png'))
print(len(slides), 'slides in', OUT)
