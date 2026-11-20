# The print on the icon's speaker cone: a small DSS1 (Michroma) below the dust cap, laid out for a top-down projection over the cone. White on transparent; render_icon.py inks it in silver.
#
#   python3 tools/render/make_icon_print.py /tmp/cone_print.png
import sys
from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1] if len(sys.argv) > 1 else '/tmp/cone_print.png'
S = 2048
img = Image.new('RGBA', (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(img)
f = ImageFont.truetype('assets/fonts/Michroma-Regular.ttf', 105)
w = d.textlength('DSS1', font=f)
d.text(((S - w) / 2, S * 0.70), 'DSS1', font=f, fill=(255, 255, 255, 255))
img.save(out)
