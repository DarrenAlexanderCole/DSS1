# Builds the DSS1 V2 Beta Guide (PDF) from the screenshots in docs/screenshots and a few close-up crops.
#
#   python3 tools/guide/make_guide.py <crops folder> <out.pdf>
#
# Fonts are the app's own (assets/fonts, SIL OFL): Michroma for headings, Archivo Narrow for text.
import sys, os
from reportlab.lib.pagesizes import letter
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Image, Table, TableStyle,
                                PageBreak, KeepTogether, NextPageTemplate)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CROPS = sys.argv[1] if len(sys.argv) > 1 else '/tmp/crops'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'DSS1-Beta-Guide.pdf'
FONTS = os.path.join(ROOT, 'assets', 'fonts')
SHOTS = os.path.join(ROOT, 'docs', 'screenshots')

pdfmetrics.registerFont(TTFont('Brand', os.path.join(FONTS, 'Michroma-Regular.ttf')))
pdfmetrics.registerFont(TTFont('Text', os.path.join(FONTS, 'ArchivoNarrow-Medium.ttf')))
pdfmetrics.registerFont(TTFont('TextBold', os.path.join(FONTS, 'ArchivoNarrow-Bold.ttf')))
pdfmetrics.registerFont(TTFont('Display', os.path.join(FONTS, 'RajdhaniTab-SemiBold.ttf')))
pdfmetrics.registerFontFamily('Text', normal='Text', bold='TextBold', italic='Text', boldItalic='TextBold')

INK = colors.HexColor('#1d1a17')
SOFT = colors.HexColor('#5b544c')
AMBER = colors.HexColor('#d9822b')
RED = colors.HexColor('#b8382a')
PAPER = colors.HexColor('#f6f2ec')
RULE = colors.HexColor('#d8cfc3')
DARK = colors.HexColor('#121010')

W, H = letter
M = 0.75 * inch

h1 = ParagraphStyle('h1', fontName='Brand', fontSize=20, leading=26, textColor=INK, spaceAfter=4)
kicker = ParagraphStyle('kicker', fontName='TextBold', fontSize=9.5, leading=12, textColor=RED, spaceAfter=10)
h2 = ParagraphStyle('h2', fontName='TextBold', fontSize=13, leading=17, textColor=INK, spaceBefore=10, spaceAfter=4)
body = ParagraphStyle('body', fontName='Text', fontSize=10.5, leading=14.5, textColor=INK, spaceAfter=6)
bullet = ParagraphStyle('bullet', parent=body, leftIndent=14, bulletIndent=2, spaceAfter=3.5)
small = ParagraphStyle('small', parent=body, fontSize=9, leading=12, textColor=SOFT)
caption = ParagraphStyle('caption', parent=small, spaceBefore=3, spaceAfter=10)
cell = ParagraphStyle('cell', parent=body, fontSize=9.8, leading=12.5, spaceAfter=0)
cellb = ParagraphStyle('cellb', parent=cell, fontName='TextBold')


def page_bg(c, doc):
    c.saveState()
    c.setFillColor(PAPER)
    c.rect(0, 0, W, H, stroke=0, fill=1)
    # A walnut-dark strip down the left edge, like the machine's cheeks.
    c.setFillColor(colors.HexColor('#2a1a10'))
    c.rect(0, 0, 0.18 * inch, H, stroke=0, fill=1)
    c.setFont('Brand', 8)
    c.setFillColor(SOFT)
    c.drawString(M, 0.45 * inch, 'DSS1  V2 BETA GUIDE')
    c.drawRightString(W - M, 0.45 * inch, str(doc.page))
    c.setStrokeColor(RULE)
    c.setLineWidth(0.6)
    c.line(M, 0.62 * inch, W - M, 0.62 * inch)
    c.restoreState()


def cover_bg(c, doc):
    c.saveState()
    c.setFillColor(DARK)
    c.rect(0, 0, W, H, stroke=0, fill=1)
    c.drawImage(os.path.join(SHOTS, '05_chop.jpg'), -0.6 * inch, H * 0.37, width=W + 1.2 * inch,
                height=(W + 1.2 * inch) * 980 / 1440, preserveAspectRatio=True, mask='auto')
    # Fade the picture into the dark below.
    for i in range(40):
        c.setFillColor(colors.Color(0.07, 0.063, 0.063, alpha=i / 40))
        c.rect(0, H * 0.37 + (40 - i) * 4, W, 4, stroke=0, fill=1)
    c.setFillColor(colors.HexColor('#f3ece2'))
    c.setFont('Brand', 54)
    c.drawString(M, 2.9 * inch, 'DSS1')
    c.setFont('TextBold', 13)
    c.setFillColor(AMBER)
    c.drawString(M, 2.45 * inch, 'DIGITAL SAMPLE STATION  ·  V2 BETA')
    c.setFont('Text', 15)
    c.setFillColor(colors.HexColor('#d9d0c4'))
    c.drawString(M, 1.95 * inch, 'The guide: install it, then sort, prep, shape, chop and deliver your music.')
    c.setFont('Text', 10)
    c.setFillColor(colors.HexColor('#8c8378'))
    c.drawString(M, 0.8 * inch, 'For macOS 13 or later, Apple Silicon and Intel.  github.com/DarrenAlexanderCole/DSS1')
    c.restoreState()


def shot(name, width=W - 2 * M, folder=SHOTS):
    path = os.path.join(folder, name)
    from PIL import Image as PImage
    w, h = PImage.open(path).size
    return Image(path, width=width, height=width * h / w)


def bullets(items):
    return [Paragraph(t, bullet, bulletText='•') for t in items]


def table(rows, widths, head=True):
    data = [[Paragraph(a, cellb), Paragraph(b, cell)] for a, b in rows]
    t = Table(data, colWidths=widths)
    t.setStyle(TableStyle([
        ('VALIGN', (0, 0), (-1, -1), 'TOP'),
        ('LINEBELOW', (0, 0), (-1, -1), 0.5, RULE),
        ('TOPPADDING', (0, 0), (-1, -1), 4),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 4),
        ('LEFTPADDING', (0, 0), (-1, -1), 2),
    ]))
    return t


def section(title, sub):
    return [Paragraph(title, h1), Paragraph(sub.upper(), kicker)]


doc = BaseDocTemplate(OUT, pagesize=letter, leftMargin=M, rightMargin=M, topMargin=M, bottomMargin=0.85 * inch,
                      title='DSS1 V2 Beta Guide', author='DSS1', subject='How to install and use DSS1 V2')
frame = Frame(M, 0.85 * inch, W - 2 * M, H - M - 0.85 * inch, id='f', leftPadding=0, rightPadding=0)
doc.addPageTemplates([PageTemplate(id='cover', frames=[frame], onPage=cover_bg),
                      PageTemplate(id='page', frames=[frame], onPage=page_bg)])
FULL = W - 2 * M
s = [NextPageTemplate('page'), PageBreak()]

# --- Install ------------------------------------------------------------------------------------------------------
s += section('Welcome', 'What DSS1 is, and how to install it')
s.append(Paragraph('DSS1 is one place to get your music ready before the show or the session. Bring in your '
                   'downloads, let DSS1 work out the tempo, key and beat grid, set your cues and loops, listen through '
                   'real effects, chop sounds onto pads, and send it all to Rekordbox, Serato or your MPC. '
                   '<b>Your music files are never changed:</b> DSS1 only ever writes copies.', body))
s.append(Paragraph('Install', h2))
s += bullets([
    'Download the <b>DSS1 .dmg</b> from the latest release on GitHub '
    '(github.com/DarrenAlexanderCole/DSS1/releases) and open it.',
    'Drag <b>DSS1</b> onto <b>Applications</b>. If you have DSS1 1.5 and want to keep it, rename it first '
    '(for example <i>DSS1 1.5</i>), because both are called DSS1.',
    'Open DSS1. The beta isn\'t notarised by Apple yet, so macOS stops it the first time. On <b>macOS 15 or later</b>: '
    'System Settings → Privacy &amp; Security, scroll to the message about DSS1, click <b>Open Anyway</b> and confirm. '
    'On <b>macOS 13–14</b>: right-click DSS1 in Applications, choose <b>Open</b>, then <b>Open</b> again. You only do this once.',
])
s.append(Paragraph('First launch', h2))
s.append(Paragraph('DSS1 opens under its smoked-glass dust cover. Click it (or press any key) and the lid lifts away. '
                   'The displays and meters warm up like an old amplifier, then you\'re in the Catalog.', body))
s.append(shot('01_cover.jpg', FULL * 0.62))
s.append(Paragraph('The dust cover, before it lifts.', caption))
s.append(Paragraph('Around every screen', h2))
s += bullets([
    '<b>Top:</b> the five stage keys (Catalog, Prep, Listen / Design, Chop, Deliver), VU meters, the track display '
    '(title, artist, BPM with four beat lights, key, time, bar) and the transport: |&lt;, &lt;&lt;, &gt;&gt;, CUE and PLAY.',
    '<b>Bottom:</b> the whole-track overview (click to jump), the pitch fader with its range (8, 16 or 50%), RESET and KEY LOCK.',
    'The <b>PAD</b> switch by Power takes 20 dB off the VU meters so loud masters don\'t pin the needles.',
])
s.append(PageBreak())

# --- Catalog ------------------------------------------------------------------------------------------------------
s += section('Catalog', 'Your music, sorted')
s.append(shot('02_catalog.jpg'))
s.append(Paragraph('The Catalog with a crate of demo tracks.', caption))
s += bullets([
    '<b>Add music:</b> drag files, folders or record-pool zips onto the window, or click <b>ADD MUSIC</b>. WAV, AIFF, '
    'FLAC and MP3 work; zips are unpacked into Music → DSS1 Imports.',
    '<b>Analysis:</b> BPM, key (Camelot and name), loudness and the beat grid fill in by themselves, a couple of seconds a track.',
    '<b>Sort</b> by clicking any column heading. Title, artist, album and genre come from the files\' tags.',
    '<b>NOTES:</b> double-click a track\'s note to type your own (Return saves). Notes sort like any column and go to '
    'Rekordbox as the track\'s Comments.',
    '<b>Watched folders:</b> a folder you add is watched; new tracks appear on their own. <b>FOLDERS</b> lists them, '
    'with <i>Stop Watching</i> and <i>Remove Folder</i> for ones you\'re done with.',
    '<b>Crates:</b> every folder you work from is a crate. The <b>CRATE</b> window shows the one you\'re in; &lt; and '
    '&gt; step through All Music and your crates, and clicking it lists them. <b>START FRESH</b> starts a new crate '
    'from a folder or clears the Catalog. Nothing is deleted: tracks keep their cues, loops and notes and come back '
    'with their crate.',
    '<b>Play:</b> double-click a track (or select it and press Return). Space plays and pauses on every screen.',
    'Right-click a track to <b>Show in Finder</b>, <b>Analyse Again</b>, <b>Export</b> it, or remove it from DSS1 '
    '(the file stays where it is).',
])
s.append(PageBreak())

# --- Prep ---------------------------------------------------------------------------------------------------------
s += section('Prep', 'Cues, loops and the beat grid')
s.append(shot('03_prep.jpg', FULL * 0.9))
s.append(Paragraph('Hot cues on the waveform and their pads below, a saved loop (blue), and the grid with bar numbers.', caption))
s += bullets([
    '<b>Waveform:</b> scroll or pinch to zoom around the mouse, drag to move, click to jump. <b>WHOLE</b> shows the whole track.',
    '<b>Hot cues A–H:</b> press a letter (or click its pad) to set it at the playhead; again to jump. Shift deletes. '
    'Right-click a cue to rename or recolour it.',
    '<b>Memory cues:</b> + MEMORY (or M); &lt; MEM and MEM &gt; step through them.',
    '<b>Loops:</b> set the length with the <b>LOOP</b> knob and press SAVE LOOP (or L); turning the knob while a loop '
    'plays resizes it live. Saved loops 1–8 play from their keys. EXIT LOOP (Escape) carries on.',
    '<b>Grid:</b> BEAT 1 makes the playhead the downbeat, NUDGE shifts it, /2 and X2 fix half or double tempo, and '
    'TEMPO CHANGE handles tracks that drift. The sixteen lights in the GRID window step along with the bar.',
    '<b>Target:</b> choose Rekordbox or Serato and DSS1 tells you anything that app can\'t take.',
])
s.append(PageBreak())

# --- Listen / Design ----------------------------------------------------------------------------------------------
s += section('Listen / Design', 'Shape the sound, and record it')
s.append(shot('04_listen.jpg', FULL * 0.8))
s.append(Paragraph('Everything you hear plays through the effects. Flip a module\'s switch to turn it on; double-click a '
                   'knob to reset it; ALL OFF turns everything off.', caption))
s.append(table([
    ('FILTER', 'One knob: low-pass to the left, high-pass to the right, open in the middle.'),
    ('ECHO', 'TIME in beats (in sync with the track), FEEDBACK and MIX. <b>PING</b> bounces the repeats left and right; '
             '<b>TAPE</b> makes it a worn tape echo (turn FEEDBACK up for a dub howl); <b>DUCK</b> keeps repeats down while '
             'the music plays; <b>FREEZE</b> loops what\'s in the echo until you turn it off.'),
    ('REVERB', 'SIZE and MIX. <b>GATE</b> slams the room shut after each hit (the 80s gated snare); <b>HALO</b> lifts the '
               'tail an octave into a shimmer.'),
    ('COMP', 'An opto compressor with LOW / MID / HIGH EQ in front, AMOUNT, ATTACK, RELEASE and dry/wet MIX; the monitors '
             'show the EQ curve, the squeeze, the envelope and the mix. <b>DRIVE</b> saturates (grit to fuzz), '
             '<b>PUNCH</b> makes hits jump out, <b>SQUASH</b> pumps hard, <b>CRUSH</b> is 12-bit sampler grit.'),
], [0.95 * inch, FULL - 0.95 * inch]))
s.append(Spacer(1, 8))
s.append(Paragraph('The recorder', h2))
s += bullets([
    '<b>REC</b> records what you hear, effects and all. Then <b>PLAY</b> it back, <b>SAVE</b> it (Music → DSS1 Recordings, '
    'and into the Catalog) or <b>DISCARD</b> it. <b>TO CHOP</b> saves it and opens it on the Chop screen.',
    '<b>-10 / -20 / -50</b> lower what the recorder takes in; turn <b>MONITOR</b> on to hear exactly what\'s going onto the take.',
    'The <b>XY pad</b> sweeps the filter (left/right) and adds echo (up) while you hold it.',
])
s.append(PageBreak())

# --- Chop ---------------------------------------------------------------------------------------------------------
s += section('Chop', 'Slice anything onto 128 pads')
s.append(shot('05_chop.jpg', FULL * 0.6))
s.append(Paragraph('Bank A chopped into 16 slices; the LEVEL lights show each pad as it plays.', caption))
s += bullets([
    '<b>What to chop:</b> the track on the deck, a sound file (<b>LOAD...</b>) or a recording (<b>TO CHOP</b>). Each bank '
    'A–H chops one sound into 16 slices.',
    '<b>Tap to chop:</b> press Space to play, then tap an <b>empty pad</b> on each hit you want; a <b>marked pad</b> plays '
    'its slice. Option-tap moves a pad\'s start to the playhead. Or let <b>TRANSIENTS</b> or <b>EQUAL 16</b> do it.',
    '<b>SLICE</b> knob: beats per slice for EQUAL 16. At <b>MM</b> (manual) each slice simply runs to the next one.',
    '<b>Fine-tune:</b> drag a marker, or nudge it with the arrow keys. <b>SNAP</b> lands markers on zero crossings or the '
    'beat; Cmd+Z undoes.',
    '<b>Each pad</b> has GAIN, TUNE, ATTACK, FADE and REVERSE, with a backlit monitor above each knob. <b>CHOKE</b> lets one '
    'slice play at a time; <b>HOLD</b> plays a slice only while held.',
    '<b>SAVE BANK</b> writes the slices as WAVs to the SAVE TO folder; <b>SHOW SAVED</b> opens it.',
])
s.append(Spacer(1, 4))
keysHead = Paragraph('Keys and controllers', h2)
keys = Table([[Paragraph(f'<font name="Display" size="12">{n}</font><br/><font size="8" color="#5b544c">pad {p}</font>', cell)
               for n, p in row] for row in [[('1', 13), ('2', 14), ('3', 15), ('4', 16)], [('Q', 9), ('W', 10), ('E', 11), ('R', 12)],
                                             [('A', 5), ('S', 6), ('D', 7), ('F', 8)], [('Z', 1), ('X', 2), ('C', 3), ('V', 4)]]],
             colWidths=[0.55 * inch] * 4, rowHeights=[0.42 * inch] * 4)
keys.setStyle(TableStyle([('GRID', (0, 0), (-1, -1), 0.6, RULE), ('BACKGROUND', (0, 0), (-1, -1), colors.HexColor('#ece5da')),
                          ('ALIGN', (0, 0), (-1, -1), 'CENTER'), ('VALIGN', (0, 0), (-1, -1), 'MIDDLE')]))
side = [Paragraph('The pads are laid out like a drum machine. Each pad shows its key until you\'ve played it. '
                  'Press <b>0</b> to stop everything.', body),
        Paragraph('Plug in a pad controller (Akai MPC, MPD, MPK or anything that sends notes) and its pads play DSS1\'s; '
                  'harder hits play louder. <b>MIDI LEARN</b> maps your own; right-click it for preset layouts.', body)]
s.append(KeepTogether([keysHead, Table([[keys, side]], colWidths=[2.45 * inch, FULL - 2.45 * inch],
               style=TableStyle([('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (1, 0), (1, 0), 14)]))]))
s.append(PageBreak())

# --- Deliver ------------------------------------------------------------------------------------------------------
s += section('Deliver', 'Send it to Rekordbox or Serato')
s.append(shot('06_deliver.jpg', FULL * 0.66))
s.append(Paragraph('Choose which tracks go: the ones you\'ve prepared, all of them, or the ones selected in the Catalog.', caption))
s += bullets([
    '<b>Export for Rekordbox</b> writes one XML file pointing at your music, with cues, loops, grids and notes. In Rekordbox: '
    'Preferences → Advanced → Database → rekordbox xml, choose the file, then in the sidebar open rekordbox xml → '
    'Playlists → DSS1, select the tracks, right-click → Import To Collection.',
    '<b>Export Copies for Serato</b> copies the tracks into Music → DSS1 for Serato with your cues and loops inside. Drag that '
    'folder into Serato. Turn on <b>BEAT GRID</b> to include the grid. (FLAC isn\'t supported for Serato yet.)',
    '<b>CHANGE...</b> picks another destination. DSS1 never replaces an original file.',
])
s.append(Spacer(1, 6))

# --- Keys ---------------------------------------------------------------------------------------------------------
s.append(KeepTogether([Paragraph('Handy keys', h2), table([
    ('Space or P', 'Play / pause, on every screen'),
    ('Q', 'CUE'),
    ('A–H (Prep)', 'Set or jump to a hot cue; Shift deletes'),
    ('M · L · Esc', 'Memory cue · save loop · exit loop (Prep)'),
    ('[ ]', 'Beat-jump back / forward'),
    ('Pad keys (Chop)', '1–4, Q–R, A–F, Z–V; Option + key re-marks the pad'),
    ('0 (Chop)', 'Stop everything'),
    ('Cmd+Z (Chop)', 'Undo'),
], [1.5 * inch, FULL - 1.5 * inch])]))
s.append(Spacer(1, 8))
s.append(KeepTogether([Paragraph('Good to know', h2)] + bullets([
    'DSS1 never edits, moves or renames your music. Exports are copies or a separate XML file.',
    'Everything saves as you go: cues, loops, grids, notes, chops and pad settings are there next time.',
    'Unsaved recordings aren\'t lost: a take left unsaved when you quit is kept in Music → DSS1 Recordings → Unsaved Takes.',
    'Where files go: imports to Music → DSS1 Imports, recordings to Music → DSS1 Recordings, saved banks to '
    'Music → DSS1 Samples, exports to Music → DSS1 Exports and Music → DSS1 for Serato.',
    'The beta is for macOS only for now, and isn\'t notarised yet (see Install).',
    'Still using DSS1 1.5? It\'s on the Releases page as <b>DSS1 1.5 (Beta)</b>; each version keeps its own library.',
])))
s.append(Spacer(1, 4))
s.append(KeepTogether([Paragraph('Tell us what you find', h2),
    Paragraph('This is a beta, so your feedback shapes it. Open an issue at github.com/DarrenAlexanderCole/DSS1/issues, '
              'ideally with a screenshot. Most useful: whether BPM, key and the grid are right for tracks you know, '
              'whether your cues and loops arrive in Rekordbox and Serato, and anything confusing, slow or broken.', body)]))

doc.build(s)
print('wrote', OUT)
