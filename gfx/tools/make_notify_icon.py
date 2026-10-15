# The icon on the in-game sign-out reminder (Ultrahand shows it beside the note): a sign-out arrow, an open door frame with an
# arrow leaving it, in one colour (white), like the console's own icons; the user picked it as A from notify_icon_mockups.py.
# Drawn 8x and shrunk to Ultrahand's 50x50, then written as
#   sysmodule/source/notify_icon.h   the raw RGBA bytes the reminder installs as config/ultrahand/assets/notifications/washi.rgba
#   gfx/tools/out/notify_icon*.png   previews
# Run from the project root: python gfx/tools/make_notify_icon.py
import os
from PIL import Image, ImageDraw

S = 8   # drawing scale
N = 50  # Ultrahand's notification icon is 50x50
W = N * S
WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)

img = Image.new('RGBA', (W, W), CLEAR)
d = ImageDraw.Draw(img)


def line(pts, w):  # a stroke in 50-px units, with round caps and joins
    p = [(x * S, y * S) for x, y in pts]
    d.line(p, fill=WHITE, width=int(w * S))
    for x, y in p:
        d.ellipse([x - w * S / 2, y - w * S / 2, x + w * S / 2, y + w * S / 2], fill=WHITE)


line([(24, 8), (11, 8), (11, 42), (24, 42)], 4.5)  # the door frame
line([(21, 25), (41, 25)], 4.5)                    # the arrow, on its way out
line([(33, 17), (41, 25), (33, 33)], 4.5)

# shrunk with premultiplied alpha, so the edges fade to clear rather than to grey
small = img.convert('RGBa').resize((N, N), Image.LANCZOS).convert('RGBA')

root = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(root, 'out')
os.makedirs(out, exist_ok=True)
small.save(os.path.join(out, 'notify_icon.png'))
for name, bg in (('notify_icon_on_dark.png', (40, 40, 44, 255)), ('notify_icon_on_grey.png', (120, 120, 128, 255))):
    prev = Image.new('RGBA', (N * 4 + 120, N * 4 + 20), bg)
    prev.alpha_composite(small.resize((N * 4, N * 4), Image.NEAREST), (10, 10))
    prev.alpha_composite(small, (N * 4 + 40, 10 + N * 2 - N // 2))
    prev.save(os.path.join(out, name))

raw = small.tobytes()  # RGBA8888, row by row: what Ultrahand reads
assert len(raw) == N * N * 4
hdr = os.path.join(root, '..', '..', 'sysmodule', 'source', 'notify_icon.h')
with open(hdr, 'w', newline='\n') as f:
    f.write('// The sign-out reminder\'s icon for Ultrahand: 50x50 RGBA8888, made by gfx/tools/make_notify_icon.py (don\'t edit).\n')
    f.write('#pragma once\n#define NOTIFY_ICON_DIM 50\n')
    f.write('static const unsigned char NOTIFY_ICON[%d] = {\n' % len(raw))
    for i in range(0, len(raw), 24):
        f.write('    ' + ', '.join('%d' % b for b in raw[i:i + 24]) + ',\n')
    f.write('};\n')
print('ok', len(raw))
