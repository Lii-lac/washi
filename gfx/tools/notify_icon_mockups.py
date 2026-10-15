# Mockups of the in-game reminder's icon: each candidate drawn at 8x and shrunk to Ultrahand's 50x50, cut down to RGBA4444 the way
# Ultrahand loads it, and shown in a note laid out as Ultrahand draws one (libultrahand tesla.cpp, NotificationPrompt::drawSlot):
# a 448 px panel, 88 tall plus a line per extra line of text, black at 13/15 over the game, the icon at 19,(h-50)/2, the title at
# 18 px and the time in grey on its right, the message under it at the note's font_size (24), wrapped at 329 px.
# Writes test/out/notify-mockups/*.png. Run from the project root: python gfx/tools/notify_icon_mockups.py
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 8
N = 50
W = N * S
WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)
BLUE_TOP, BLUE_BOT = (0x34, 0xCB, 0xF5), (0x0A, 0x9C, 0xD6)  # the logo's label (skin_logo_mark)
ALEX = (0x3D, 0x8B, 0xD9)                                       # a profile colour, for the personal ones
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'test', 'out', 'notify-mockups')
FONT = 'C:/Windows/Fonts/segoeui.ttf'      # stands in for the console's own font
FONT_BOLD = 'C:/Windows/Fonts/segoeuib.ttf'


def U(v):
    return v * S


class Icon:
    def __init__(self):
        self.img = Image.new('RGBA', (W, W), CLEAR)
        self.d = ImageDraw.Draw(self.img)

    def rr(self, x0, y0, x1, y1, r, fill=WHITE):
        self.d.rounded_rectangle([U(x0), U(y0), U(x1), U(y1)], radius=U(r), fill=fill)

    def circle(self, cx, cy, r, fill=WHITE):
        self.d.ellipse([U(cx - r), U(cy - r), U(cx + r), U(cy + r)], fill=fill)

    def line(self, pts, w, fill=WHITE):  # round caps and joins
        p = [(U(x), U(y)) for x, y in pts]
        self.d.line(p, fill=fill, width=int(U(w)))
        for x, y in p:
            self.d.ellipse([x - U(w) / 2, y - U(w) / 2, x + U(w) / 2, y + U(w) / 2], fill=fill)

    def arc(self, cx, cy, r, a0, a1, w, fill=WHITE):
        self.d.arc([U(cx - r), U(cy - r), U(cx + r), U(cy + r)], a0, a1, fill=fill, width=int(U(w)))

    def text(self, cx, cy, s, size, fill=WHITE, font=FONT_BOLD):
        f = ImageFont.truetype(font, int(U(size)))
        self.d.text((U(cx), U(cy)), s, font=f, fill=fill, anchor='mm')

    def tint(self, rgb):  # a white drawing in another single colour
        r, g, b = rgb
        a = self.img.getchannel('A')
        self.img = Image.merge('RGBA', (a.point(lambda _: r), a.point(lambda _: g), a.point(lambda _: b), a))
        return self

    def small(self):
        return self.img.convert('RGBa').resize((N, N), Image.LANCZOS).convert('RGBA')


def gradient(w, h, top, bot):
    g = Image.new('RGBA', (w, h))
    for y in range(h):
        t = y / max(1, h - 1)
        g.paste(tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,), (0, y, w, y + 1))
    return g


# ---- the candidates (shapes in 50-px units)

def sign_out():
    i = Icon()
    i.line([(24, 8), (11, 8), (11, 42), (24, 42)], 4.5)   # the door frame
    i.line([(21, 25), (41, 25)], 4.5)                     # the arrow, on its way out
    i.line([(33, 17), (41, 25), (33, 33)], 4.5)
    return i


def person_leaving():
    i = Icon()
    i.circle(18, 14, 6.5)
    i.rr(6, 24, 30, 46, 11)
    i.rr(6, 36, 30, 46, 0, CLEAR)                          # (flat at the bottom)
    i.rr(6, 35, 30, 36.5, 0)
    i.line([(32, 30), (44, 30)], 4)
    i.line([(39, 25), (44, 30), (39, 35)], 4)
    return i


def locker():
    i = Icon()
    i.rr(12, 4, 38, 46, 4)
    for k in range(3):                                     # the vents
        i.rr(17, 9.5 + k * 4.2, 33, 11.5 + k * 4.2, 1, CLEAR)
    i.rr(30.5, 25, 33.5, 35, 1.5, CLEAR)                   # the handle
    return i


def padlock_open():
    i = Icon()
    i.line([(16, 24), (16, 13)], 4.5)                      # the shackle, swung open
    i.arc(25, 13, 9, 180, 360, 4.5)
    i.line([(34, 13), (34, 15.5)], 4.5)
    i.rr(10, 22, 40, 46, 5)
    i.circle(25, 31.5, 3.4, CLEAR)                         # the keyhole
    i.rr(23.6, 32, 26.4, 39.5, 1, CLEAR)
    return i


def cartridge():  # Washi's mark, bolder: the label holds the person, and four contacts below
    i = Icon()
    i.rr(10.5, 3, 39.5, 47, 5)
    i.rr(14, 7, 36, 33, 3, CLEAR)
    i.circle(25, 15.5, 4.6)
    i.rr(17.4, 22.2, 32.6, 33, 5)
    i.rr(14, 30.5, 36, 33, 0, CLEAR)                       # (the label's edge cuts the shoulders)
    for k in range(4):
        i.rr(15.5 + k * 5.2, 37, 18.5 + k * 5.2, 43.5, 1, CLEAR)
    return i


def floppy():
    i = Icon()
    i.d.polygon([(U(7), U(9)), (U(9), U(7)), (U(37), U(7)), (U(43), U(13)), (U(43), U(41)), (U(41), U(43)), (U(9), U(43)), (U(7), U(41))], fill=WHITE)
    i.rr(15, 7, 33, 18, 1, CLEAR)                          # the shutter
    i.rr(26.5, 9.5, 30.5, 15.5, 0.5)
    i.rr(12, 25, 38, 40, 2, CLEAR)                         # the label
    return i


def bell():
    i = Icon()
    i.circle(25, 8, 2.6)
    i.d.pieslice([U(13), U(9), U(37), U(33)], 180, 360, fill=WHITE)
    i.d.polygon([(U(13), U(21)), (U(37), U(21)), (U(40), U(35)), (U(10), U(35))], fill=WHITE)
    i.rr(7, 34, 43, 38.5, 2.2)
    i.circle(25, 42.5, 3.8)
    return i


def initial_ring(letter='A'):
    i = Icon()
    i.arc(25, 25, 21, 0, 360, 3.6)
    i.text(25, 24.2, letter, 25)
    return i


def logo_colour():  # the app's own mark, in its colours (skin_logo_mark)
    i = Icon()
    k = 46 / 784.0
    ox, oy = (50 - 520 * k) / 2, 2
    def box(x, y, w, h, r, fill):
        i.rr(ox + x * k, oy + y * k, ox + (x + w) * k, oy + (y + h) * k, r * k, fill)
    m = Icon()
    m.rr(ox, oy, ox + 520 * k, oy + 46, 64 * k)
    i.img.alpha_composite(Image.composite(gradient(W, W, (0x55, 0x5C, 0x67), (0x23, 0x27, 0x2E)), Image.new('RGBA', (W, W), CLEAR), m.img.getchannel('A')))
    lab = Icon()
    lab.rr(ox + 44 * k, oy + 56 * k, ox + 476 * k, oy + 526 * k, 36 * k)
    i.img.alpha_composite(Image.composite(gradient(W, W, BLUE_TOP, BLUE_BOT), Image.new('RGBA', (W, W), CLEAR), lab.img.getchannel('A')))
    i.d = ImageDraw.Draw(i.img)
    ps, px, py = 270, 260, 56 + 205
    i.circle(ox + px * k, oy + (py - ps * 0.24) * k, ps * 0.24 * k)
    box(px - ps * 0.42, py + ps * 0.06, ps * 0.84, ps * 0.42, ps * 0.21, WHITE)
    box(44, 566, 432, 150, 28, (0x15, 0x18, 0x1D, 255))
    for n in range(5):
        box(76 + n * 82, 600, 52, 100, 6, (0xD9, 0xB3, 0x6A, 255))
    return i


def initial_on_colour(letter='A'):
    i = Icon()
    i.circle(25, 25, 23, ALEX + (255,))
    i.text(25, 24, letter, 27)
    return i


def sign_out_tile():
    i = Icon()
    t = Icon()
    t.rr(2, 2, 48, 48, 11)
    i.img.alpha_composite(Image.composite(gradient(W, W, BLUE_TOP, BLUE_BOT), Image.new('RGBA', (W, W), CLEAR), t.img.getchannel('A')))
    g = sign_out().img.resize((int(W * 0.7), int(W * 0.7)), Image.LANCZOS)
    i.img.alpha_composite(g, (int(W * 0.17), int(W * 0.15)))
    return i


def current():
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out', 'notify_icon.png')
    i = Icon()
    i.img = Image.open(p).convert('RGBA').resize((W, W), Image.NEAREST)
    return i


CANDIDATES = [
    ('now', 'Now: the mark in white', current, 'now'),
    ('A', 'Sign-out arrow', sign_out, 'mono'),
    ('B', 'Person leaving', person_leaving, 'mono'),
    ('C', 'A locker', locker, 'mono'),
    ('D', 'Open padlock', padlock_open, 'mono'),
    ('E', 'The cartridge, bolder', cartridge, 'mono'),
    ('F', 'Floppy disk', floppy, 'mono'),
    ('G', 'Bell', bell, 'mono'),
    ('H', 'Their initial in a ring', initial_ring, 'mono'),
    ('I', 'The cartridge in blue', lambda: cartridge().tint(BLUE_TOP), 'mono-blue'),
    ('J', 'Sign-out arrow in blue', lambda: sign_out().tint(BLUE_TOP), 'mono-blue'),
    ('K', 'The app\'s own mark, in colour', logo_colour, 'colour'),
    ('L', 'Their initial on their colour', initial_on_colour, 'colour'),
    ('M', 'Sign-out arrow on a blue tile', sign_out_tile, 'colour'),
]


def rgba4444(img):  # Ultrahand keeps the top 4 bits of each channel (ult::loadRGBA8888toRGBA4444)
    return img.point(lambda v: (v >> 4) * 17)


# ---- a game behind the note, and the note

def game_scene():
    sc = gradient(1280, 720, (120, 190, 245), (225, 240, 255))
    d = ImageDraw.Draw(sc)
    d.ellipse([980, 60, 1120, 200], fill=(255, 236, 150, 255))
    d.ellipse([-300, 420, 700, 1100], fill=(92, 178, 92, 255))
    d.ellipse([400, 470, 1600, 1150], fill=(70, 156, 80, 255))
    for x, y, c in ((150, 230, (250, 250, 250)), (560, 150, (245, 245, 250)), (760, 280, (250, 250, 255))):
        d.ellipse([x, y, x + 220, y + 70], fill=c + (255,))
        d.ellipse([x + 50, y - 35, x + 170, y + 50], fill=c + (255,))
    d.rectangle([180, 520, 330, 640], fill=(214, 120, 60, 255))
    d.rectangle([180, 520, 330, 540], fill=(170, 80, 40, 255))
    return sc.filter(ImageFilter.GaussianBlur(1.2))


def font_px(path, px):  # stb_truetype's ScaleForPixelHeight: ascent - descent = px
    probe = ImageFont.truetype(path, 100)
    a, dsc = probe.getmetrics()
    f = ImageFont.truetype(path, max(1, round(px * 100 / (a + dsc))))
    return f


def wrap(text, f, width):
    words, lines, cur = text.split(' '), [], ''
    for w in words:
        t = (cur + ' ' + w).strip()
        if f.getlength(t) <= width or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = w
    lines.append(cur)
    return lines


def note(scene, icon50, title, msg, time='14:14', font_size=24):
    tf, mf = font_px(FONT, 18), font_px(FONT, font_size)
    lines = wrap(msg, mf, 448 - 88 - 19 - 2 - 10)
    t_lh, m_lh = 18, font_size
    h = 88 + (len(lines) - 1) * (m_lh + 3)
    panel = scene.crop((0, 0, 448, h)).copy()
    panel.alpha_composite(Image.new('RGBA', (448, h), (0, 0, 0, round(255 * 13 / 15))))
    icon = rgba4444(icon50)
    panel.alpha_composite(icon, (19, (h - 50) // 2))
    d = ImageDraw.Draw(panel)
    block = t_lh + 4 + len(lines) * m_lh + (len(lines) - 1) * 3
    oy = (h - block) // 2
    d.text((88, oy), title, font=tf, fill=(255, 255, 255, 255), anchor='lt')
    tw = d.textlength(time, font=tf)
    d.text((448 - 19 - 2 - tw, oy), time, font=tf, fill=(0xAA, 0xAA, 0xAA, 255), anchor='lt')
    for k, ln in enumerate(lines):
        d.text((88, oy + t_lh + 4 + 3 + k * (m_lh + 3)), ln, font=mf, fill=(255, 255, 255, 255), anchor='lt')
    d.rectangle([447, 0, 447, h - 1], fill=(0x30, 0x30, 0x30, 255))
    d.rectangle([0, h - 1, 447, h - 1], fill=(0x30, 0x30, 0x30, 255))
    return panel


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    scene = game_scene()
    msg = "Don't forget to sign out when you're done playing!"
    for key, label, fn, kind in CANDIDATES:
        ic = fn().small()
        panel = note(scene, ic, 'Alex', msg)
        panel.resize((panel.width * 2, panel.height * 2), Image.NEAREST).save(os.path.join(OUT, 'note-%s.png' % key))
        big = Image.new('RGBA', (50, 50), (0, 0, 0, 255))
        big.alpha_composite(Image.new('RGBA', (50, 50), (24, 24, 24, 255)))
        big.alpha_composite(rgba4444(ic))
        big.resize((200, 200), Image.NEAREST).save(os.path.join(OUT, 'icon-%s.png' % key))
        if key == 'A':
            full = scene.copy()
            full.alpha_composite(panel, (0, 0))
            full.save(os.path.join(OUT, 'screen-A.png'))
    print('ok', OUT)


if __name__ == '__main__':
    main()
