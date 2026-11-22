# Vertical (9:16, 1080 x 1920) feature slides for DSS1 V2, from the screenshots in docs/screenshots, in the
# app's own fonts and colours.
#
#   python3 tools/guide/make_slides.py <shots folder (full-size PNGs)> <out folder>
import os, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHOTS = sys.argv[1] if len(sys.argv) > 1 else '/tmp/shots'
OUT = sys.argv[2] if len(sys.argv) > 2 else '/tmp/slides'
os.makedirs(OUT, exist_ok=True)
F = lambda name, size: ImageFont.truetype(os.path.join(ROOT, 'assets', 'fonts', name), size)
BRAND, BOLD, MED, DISP = 'Michroma-Regular.ttf', 'ArchivoNarrow-Bold.ttf', 'ArchivoNarrow-Medium.ttf', 'RajdhaniTab-SemiBold.ttf'
W, H = 1080, 1920
AMBER, CREAM, SOFT, RED, BG = (255, 154, 46), (243, 236, 226), (170, 160, 148), (214, 72, 52), (16, 13, 12)


def base():
    img = Image.new('RGB', (W, H), BG)
    d = ImageDraw.Draw(img)
    # Warm glow at the top, like the machine's backlight.
    glow = Image.new('L', (W, H), 0)
    ImageDraw.Draw(glow).ellipse((-300, -700, W + 300, 700), fill=70)
    glow = glow.filter(ImageFilter.GaussianBlur(160))
    img.paste(Image.new('RGB', (W, H), (90, 46, 14)), (0, 0), glow)
    # Walnut cheeks.
    d = ImageDraw.Draw(img)
    for x0 in (0, W - 18):
        d.rectangle((x0, 0, x0 + 17, H), fill=(42, 26, 16))
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


def crop(name, box):
    return Image.open(os.path.join(SHOTS, name)).convert('RGB').crop(box)


def framed(img, pic, y, width=W - 120, radius=18):
    pic = pic.resize((width, int(pic.height * width / pic.width)), Image.LANCZOS)
    x = (W - width) // 2
    shadow = Image.new('L', (W, H), 0)
    ImageDraw.Draw(shadow).rounded_rectangle((x + 8, y + 18, x + width + 8, y + pic.height + 18), radius, fill=180)
    img.paste((0, 0, 0), (0, 0), shadow.filter(ImageFilter.GaussianBlur(22)))
    mask = Image.new('L', pic.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, pic.width - 1, pic.height - 1), radius, fill=255)
    img.paste(pic, (x, y), mask)
    ImageDraw.Draw(img).rounded_rectangle((x, y, x + width - 1, y + pic.height - 1), radius, outline=(70, 60, 52), width=2)
    return y + pic.height


def header(img, step, title, kicker):
    d = ImageDraw.Draw(img)
    d.text((70, 120), 'DSS1', font=F(BRAND, 34), fill=CREAM)
    d.text((W - 70, 128), step, font=F(DISP, 34), fill=AMBER, anchor='ra')
    d.text((70, 230), kicker.upper(), font=F(BOLD, 34), fill=RED)
    y = 285
    for line in wrap(d, title, F(BRAND, 64), W - 140):
        d.text((70, y), line, font=F(BRAND, 64), fill=CREAM)
        y += 84
    return y + 30


def bullets(img, items, y, size=40):
    d = ImageDraw.Draw(img)
    font = F(MED, size)
    for head, text in items:
        d.ellipse((74, y + 17, 88, y + 31), fill=AMBER)
        lines = wrap(d, text, font, W - 200)
        d.text((112, y), head, font=F(BOLD, size), fill=AMBER)
        hx = 112 + d.textlength(head + ' ', font=F(BOLD, size))
        first = wrap(d, text, font, W - 182 - (hx - 112))
        d.text((hx, y), first[0] if first else '', font=font, fill=CREAM)
        rest = ' '.join(text.split()[len(first[0].split()):]) if first else ''
        y += int(size * 1.35)
        for line in wrap(d, rest, font, W - 182):
            d.text((112, y), line, font=font, fill=CREAM)
            y += int(size * 1.35)
        y += 22
    return y


def footer(img, text='github.com/DarrenAlexanderCole/DSS1'):
    d = ImageDraw.Draw(img)
    d.line((70, H - 150, W - 70, H - 150), fill=(60, 52, 46), width=2)
    d.text((70, H - 115), text, font=F(MED, 30), fill=SOFT)
    d.text((W - 70, H - 115), 'V2.1 BETA', font=F(BOLD, 30), fill=AMBER, anchor='ra')


slides = []

# 1. Title
img = base()
y = framed(img, crop('05_chop.png', (130, 300, 1470, 910)), 520, W - 80)
d = ImageDraw.Draw(img)
d.text((W // 2, 200), 'DSS1', font=F(BRAND, 150), fill=CREAM, anchor='ma')
d.text((W // 2, 395), 'DIGITAL SAMPLE STATION', font=F(BOLD, 44), fill=AMBER, anchor='ma')
yy = y + 110
for line in wrap(d, 'Get your music ready for the show or the session.', F(BRAND, 50), W - 160):
    d.text((W // 2, yy), line, font=F(BRAND, 50), fill=CREAM, anchor='ma')
    yy += 72
yy += 40
for line in ['Sort it. Prep it. Shape it.', 'Chop it. Deliver it.']:
    d.text((W // 2, yy), line, font=F(MED, 46), fill=SOFT, anchor='ma')
    yy += 64
d.rounded_rectangle((W // 2 - 260, H - 330, W // 2 + 260, H - 230), 50, fill=AMBER)
d.text((W // 2, H - 280), 'V2.1 BETA  ·  macOS', font=F(BOLD, 44), fill=BG, anchor='mm')
slides.append(img)

# 2. Catalog
img = base()
y = header(img, '1 / 7', 'Your music, sorted.', 'Catalog')
y = framed(img, crop('02_catalog.png', (150, 400, 1450, 810)), y)
bullets(img, [('Drop in', 'files, folders or record-pool zips.'),
              ('BPM, key, loudness', 'and the beat grid are worked out for you.'),
              ('Notes', 'on every track, and they travel to Rekordbox.'),
              ('Never changed:', 'your music files stay exactly as they are.')], y + 70)
footer(img)
slides.append(img)

# 3. Prep
img = base()
y = header(img, '2 / 7', 'Cues, loops and the grid.', 'Prep')
y = framed(img, crop('03_prep.png', (150, 385, 1450, 600)), y)
y = framed(img, crop('03_prep.png', (150, 640, 1180, 870)), y + 30)
bullets(img, [('Hot cues A–H,', 'memory cues and saved loops.'),
              ('Loops resize live', 'as you turn the LOOP knob.'),
              ('Fix the grid:', 'beat 1, nudge, half/double, tempo changes.')], y + 60)
footer(img)
slides.append(img)

# 4. Listen / Design
img = base()
y = header(img, '3 / 7', 'Shape the sound.', 'Listen / Design')
y = framed(img, crop('04_listen.png', (150, 340, 1470, 580)).crop((0, 0, 700, 240)), y)
y = framed(img, crop('04_listen.png', (150, 340, 1470, 580)).crop((610, 0, 1320, 240)), y + 30)
bullets(img, [('ECHO:', 'PING, TAPE, DUCK, FREEZE.'),
              ('REVERB:', 'GATE and HALO shimmer.'),
              ('COMP:', 'opto with EQ, plus DRIVE, PUNCH, SQUASH, CRUSH.'),
              ('Record', 'what you hear, then send it straight to Chop.')], y + 60)
footer(img)
slides.append(img)

# New in 2.1: DELUXE
img = base()
y = header(img, '4 / 7', 'A modular remix rack.', 'DELUXE  ·  new in 2.1')
y = framed(img, crop('07_deluxe.png', (126, 340, 1474, 560)).crop((0, 0, 700, 220)), y)
y = framed(img, crop('07_deluxe.png', (126, 340, 1474, 560)).crop((648, 0, 1348, 220)), y + 30)
bullets(img, [('One press', 'flips the effects into eight modules.'),
              ('STUTTER, SLICER, NOISE,', 'WAVETABLE, DRIFT, MULTI-TAP, LADDER.'),
              ('MOD', 'moves any knob in time with the music.'),
              ('Drag to reorder,', 'save patches. Starts turned down.')], y + 60, 38)
footer(img)
slides.append(img)

# New in 2.1: FTWRK!
img = base()
y = header(img, '5 / 7', 'Grab a beat. Play it in 16 steps.', 'FTWRK!  ·  new in 2.1')
y = framed(img, crop('08_ftwrk.png', (126, 340, 1474, 560)).crop((0, 0, 700, 220)), y)
y = framed(img, crop('08_ftwrk.png', (126, 340, 1474, 560)).crop((648, 0, 1348, 220)), y + 30)
bullets(img, [('GRAB', '1, 2 or 4 beats of what\'s playing.'),
              ('16 steps', 'in time with the track: click, drag, swing.'),
              ('Its own effects:', 'gate, reverse, note repeat, space echo.')], y + 60, 38)
footer(img)
slides.append(img)

# 5. Chop
img = base()
y = header(img, '6 / 7', 'Chop anything onto 128 pads.', 'Chop')
y = framed(img, crop('05_chop.png', (150, 375, 1450, 560)), y)
y = framed(img, crop('05_chop.png', (150, 590, 640, 900)), y + 30, 640)
bullets(img, [('Tap an empty pad', 'to mark a slice; tap it again to play it.'),
              ('Each pad:', 'gain, tune, attack, fade, reverse.'),
              ('Keyboard or MIDI', 'pads (Akai MPC, MPD, MPK). 0 stops all.')], y + 50, 38)
footer(img)
slides.append(img)

# 6. Deliver
img = base()
y = header(img, '7 / 7', 'Send it to your setup.', 'Deliver')
y = framed(img, crop('06_deliver.png', (150, 420, 1450, 760)), y)
bullets(img, [('Rekordbox:', 'one XML with cues, loops, grids and notes.'),
              ('Serato:', 'tagged copies, cues and loops inside.'),
              ('Chop banks', 'save as WAVs, ready for your MPC.')], y + 70)
footer(img)
slides.append(img)

# 7. Get it
img = base()
d = ImageDraw.Draw(img)
icon = Image.open(os.path.join(ROOT, 'assets', 'icon', 'dss1_icon.png')).convert('RGBA').resize((420, 420), Image.LANCZOS)
img.paste(icon, ((W - 420) // 2, 260), icon)
d.text((W // 2, 760), 'Try the beta', font=F(BRAND, 76), fill=CREAM, anchor='ma')
y = 900
for n, text in [('1', 'Download DSS1 from the Releases page on GitHub.'),
                ('2', 'Drag DSS1 into Applications.'),
                ('3', 'First open: System Settings → Privacy & Security → Open Anyway.')]:
    d.ellipse((80, y, 140, y + 60), fill=AMBER)
    d.text((110, y + 30), n, font=F(DISP, 44), fill=BG, anchor='mm')
    yy = y + 4
    for line in wrap(d, text, F(MED, 44), W - 260):
        d.text((170, yy), line, font=F(MED, 44), fill=CREAM)
        yy += 58
    y = max(y + 100, yy + 40)
d.rounded_rectangle((70, y + 40, W - 70, y + 190), 30, outline=AMBER, width=3)
d.text((W // 2, y + 82), 'github.com/', font=F(MED, 40), fill=SOFT, anchor='ma')
d.text((W // 2, y + 128), 'DarrenAlexanderCole/DSS1', font=F(BOLD, 46), fill=AMBER, anchor='ma')
d.text((W // 2, H - 200), 'macOS 13+  ·  Apple Silicon & Intel  ·  Free & open source', font=F(MED, 34), fill=SOFT, anchor='ma')
slides.append(img)

for i, s in enumerate(slides, 1):
    s.save(os.path.join(OUT, f'DSS1-slide-{i:02d}.png'))
print(len(slides), 'slides in', OUT)
