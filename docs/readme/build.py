#!/usr/bin/env python3
"""Draws every picture the README uses, in both themes, from the tokens below (DESIGN.md says why they are what they are).

    python docs/readme/build.py                   draw them all
    python docs/readme/build.py --import <dir>    first turn the PNGs that shots.sh wrote into shots/*.jpg

Needs Python 3 with Pillow, and Chrome (set CHROME if it is not found). The fonts are the app's own, from gfx/fonts_src.
Exits non-zero if a text colour on its ground falls below WCAG AA.
"""
import argparse
import base64
import io
import math
import os
import pathlib
import random
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
FONTS = HERE.parent.parent / "gfx" / "fonts_src"
SHOTS = HERE / "shots"

# Tokens: the app's own (source/gfx/paper.h) plus the console's pastel tape colours (source/gfx/paper.c).
# Two themes, as in the app: the light one (cream board) and Paper night, its dark theme (source/gfx/paper.c, pal_night).
THEMES = dict(
    light=dict(T=dict(board="#F1EBDD", paper="#FFFEFB", ink="#2D2D2D", soft="#5A5A5A", thread="#7A7590", blue="#2F3A8F",
                      strip="#D6FACA", fold="#ECFDE6", shadow="#4A3A1A", dark="#2D2D2D", crayon="#3F3A5A", button="#2D2D2D", tx="#2D2D2D"),
               TAPE=dict(pink="#FFCDCC", peach="#FFE3CD", yellow="#FFFF9A", green="#D4FAC7", mint="#BFEFE1", blue="#AED6F9",
                         lilac="#E4CCFE", rose="#FFCCFA"),
               grain=".29 .23 .10", grain_a=".95 0 0 0 -.36", sh=1.0, night=False),
    dark=dict(T=dict(board="#2A2546", paper="#29283B", ink="#EFEAF2", soft="#BDB6C8", thread="#8E86A0", blue="#B4BCF0",
                     strip="#4E5390", fold="#6B72B0", shadow="#000000", dark="#1E1F3A", crayon="#E2DCEE", button="#1E1F3A", tx="#F6F1FA"),
              TAPE=dict(pink="#C38BA5", peach="#E1B581", yellow="#E2D7B7", green="#84A894", mint="#719FA4", blue="#8394C1",
                        lilac="#9F96CD", rose="#AD85B3"),
              grain=".75 .72 .95", grain_a=".5 0 0 0 -.2", sh=1.9, night=True),
)
T, TAPE = {}, {}
THEME = "light"
OUT = HERE


def set_theme(name):
    """Switch every colour token (and the output folder) to a theme. Light pictures live in docs/readme/, dark ones in docs/readme/dark/."""
    global THEME, OUT, INKBLUE
    THEME = name
    T.clear(); T.update(THEMES[name]["T"])
    TAPE.clear(); TAPE.update(THEMES[name]["TAPE"])
    INKBLUE = T["blue"]
    OUT = HERE if name == "light" else HERE / "dark"
    OUT.mkdir(exist_ok=True)


def shot(name, theme=None):
    """A screenshot of the given theme (default: the current one)."""
    return (SHOTS if (theme or THEME) == "light" else SHOTS / "dark") / f"{name}.jpg"


set_theme("light")

# ----------------------------------------------------------------------------------------------------- contrast


def _lum(h):
    c = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    c = [v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4 for v in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def contrast(a, b):
    la, lb = sorted((_lum(a), _lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


# (text, ground, smallest size drawn in px): 4.5 under 24 px, 3.0 from 24 px up
def pairs():
    return [
        (T["tx"], T["strip"], 112), (T["ink"], T["board"], 44), (T["soft"], T["board"], 26),
        *[(T["tx"], deep(TAPE[c]) if THEMES[THEME]["night"] else TAPE[c], 30) for c in ("green", "pink", "blue", "mint", "peach", "lilac", "yellow")],
        (T["ink"], T["paper"], 21), (T["soft"], T["paper"], 20), (T["blue"], T["paper"], 17), (T["blue"], T["board"], 26),
    ]


def check_contrast():
    bad = []
    for fg, bg, px in pairs():
        need = 3.0 if px >= 24 else 4.5
        r = contrast(fg, bg)
        if r < need:
            bad.append(f"{fg} on {bg} at {px}px is {r:.2f}:1, needs {need}:1")
    if bad:
        sys.exit("contrast check failed:\n  " + "\n  ".join(bad))


# ----------------------------------------------------------------------------------------------------- pieces


def b64(path):
    return base64.b64encode(pathlib.Path(path).read_bytes()).decode()


def zig_pts(w, h, tooth=4.0, period=10.0, left=True, right=True):
    """A rectangle whose short ends are cut with pinking shears."""
    n = max(2, round(h / period))
    n += n % 2
    pts = []
    for i in range(n + 1):
        pts.append(((tooth if i % 2 else 0) if left else 0, h * i / n))
    for i in range(n, -1, -1):
        pts.append(((w - tooth if i % 2 else w) if right else w, h * i / n))
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)


def mix(a, b, k):
    return "#" + "".join(f"{round(int(a[i:i + 2], 16) * (1 - k) + int(b[i:i + 2], 16) * k):02X}" for i in (1, 3, 5))


def deep(color):
    """The tape that carries text. In Paper night it is darkened so the text can be light: dark text on mid-tone tape reads as black on a dark page."""
    return mix(color, "#12102A", .5) if THEMES[THEME]["night"] else color


def tape(cx, cy, w, h, rot, color, pat, text=False):
    pts = zig_pts(w, h)
    if text and THEMES[THEME]["night"]:
        color, pat = deep(color), "q-" + pat
    else:
        pat = "p-" + pat
    return (f'<g transform="translate({cx} {cy}) rotate({rot})" filter="url(#tapeshadow)">'
            f'<g transform="translate({-w / 2:.1f} {-h / 2:.1f})">'
            f'<polygon points="{pts}" fill="{color}" fill-opacity=".88"/>'
            f'<polygon points="{pts}" fill="url(#{pat})"/></g></g>')


def print_(cx, cy, w, rot, jpg, deep=False):
    """A screenshot as a paper print: white card, an even margin, a hairline round the picture."""
    pad = 12
    iw = w - 2 * pad
    ih = iw * 9 / 16
    ph = ih + 2 * pad
    return (f'<g transform="translate({cx} {cy}) rotate({rot})" filter="url(#{"deepshadow" if deep else "printshadow"})">'
            f'<rect x="{-w / 2}" y="{-ph / 2}" width="{w}" height="{ph:.1f}" rx="5" fill="{T["paper"]}"/>'
            f'<image href="data:image/jpeg;base64,{b64(jpg)}" x="{-iw / 2}" y="{-ph / 2 + pad:.1f}" width="{iw}" height="{ih:.1f}"/>'
            f'<rect x="{-iw / 2}" y="{-ph / 2 + pad:.1f}" width="{iw}" height="{ih:.1f}" fill="none" stroke="{T["thread"]}" stroke-opacity=".35"/>'
            f'</g>'), ph


def title_strip(x, y, w, h, rot, label, size):
    """The printed strip the app puts its name on: mint, light diagonal stripes, pinked left end, a folded pull-tab on the right."""
    fold = h * 0.5
    body = zig_pts(w - fold + 6, h, left=True, right=False)
    tab = zig_pts(fold, h * 0.84, tooth=5, left=True, right=False)
    return (f'<g transform="translate({x} {y}) rotate({rot})" filter="url(#printshadow)">'
            f'<polygon points="{body}" fill="{T["strip"]}"/>'
            f'<polygon points="{body}" fill="url(#p-printed)"/>'
            f'<g transform="translate({w - fold:.1f} {h * 0.08:.1f})"><polygon points="{tab}" fill="{T["fold"]}"/></g>'
            f'<text x="{(w - fold) / 2 + 4:.1f}" y="{h / 2:.1f}" text-anchor="middle" dominant-baseline="central" '
            f'font-family="M" font-weight="700" font-size="{size}" fill="{T["tx"]}">{label}</text></g>')


def defs():
    g = THEMES[THEME]["grain"].split()
    k = THEMES[THEME]["sh"]
    return f"""<defs>
<filter id="grain" x="0" y="0" width="100%" height="100%">
  <feTurbulence type="fractalNoise" baseFrequency=".8" numOctaves="2" seed="11" result="n"/>
  <feColorMatrix in="n" type="matrix" values="0 0 0 0 {g[0]}  0 0 0 0 {g[1]}  0 0 0 0 {g[2]}  {THEMES[THEME]["grain_a"]}"/>
</filter>
<filter id="printshadow" x="-10%" y="-10%" width="120%" height="125%">
  <feDropShadow dx="0" dy="3" stdDeviation="4" flood-color="{T["shadow"]}" flood-opacity="{.30 * k:.2f}"/>
</filter>
<filter id="deepshadow" x="-15%" y="-15%" width="130%" height="135%">
  <feDropShadow dx="0" dy="12" stdDeviation="11" flood-color="{T["shadow"]}" flood-opacity="{.34 * k:.2f}"/>
</filter>
<filter id="ink" x="-5%" y="-10%" width="110%" height="120%">
  <feTurbulence type="fractalNoise" baseFrequency=".05" numOctaves="2" seed="3" result="w"/>
  <feDisplacementMap in="SourceGraphic" in2="w" scale="2.4" result="d"/>
  <feTurbulence type="fractalNoise" baseFrequency=".9" numOctaves="1" seed="5" result="g"/>
  <feColorMatrix in="g" type="matrix" values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 -2.2 2.25" result="m"/>
  <feComposite in="d" in2="m" operator="in"/>
</filter>
<filter id="tapeshadow" x="-20%" y="-40%" width="140%" height="190%">
  <feDropShadow dx="0" dy="1.5" stdDeviation="1.6" flood-color="{T["shadow"]}" flood-opacity="{.26 * k:.2f}"/>
</filter>
<pattern id="p-stripes" width="14" height="14" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="6" height="14" fill="#fff" fill-opacity=".5"/></pattern>
<pattern id="p-dots" width="16" height="16" patternUnits="userSpaceOnUse"><circle cx="8" cy="8" r="2.6" fill="#fff" fill-opacity=".75"/></pattern>
<pattern id="p-grid" width="13" height="13" patternUnits="userSpaceOnUse"><path d="M0 .5H13M.5 0V13" stroke="#fff" stroke-opacity=".6" stroke-width="1.3" fill="none"/></pattern>
<pattern id="p-chevron" width="18" height="12" patternUnits="userSpaceOnUse"><path d="M0 9L9 3L18 9" stroke="#fff" stroke-opacity=".6" stroke-width="2.2" fill="none"/></pattern>
<pattern id="q-stripes" width="14" height="14" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="6" height="14" fill="#fff" fill-opacity=".16"/></pattern>
<pattern id="q-dots" width="16" height="16" patternUnits="userSpaceOnUse"><circle cx="8" cy="8" r="2.6" fill="#fff" fill-opacity=".34"/></pattern>
<pattern id="q-grid" width="13" height="13" patternUnits="userSpaceOnUse"><path d="M0 .5H13M.5 0V13" stroke="#fff" stroke-opacity=".28" stroke-width="1.3" fill="none"/></pattern>
<pattern id="q-chevron" width="18" height="12" patternUnits="userSpaceOnUse"><path d="M0 9L9 3L18 9" stroke="#fff" stroke-opacity=".28" stroke-width="2.2" fill="none"/></pattern>
<pattern id="p-printed" width="30" height="30" patternUnits="userSpaceOnUse" patternTransform="rotate(-38)"><rect width="13" height="30" fill="#fff" fill-opacity=".28"/></pattern>
</defs>"""


def page(w, h, body, ground=True):
    css = ""
    for weight, name in ((700, "Bold"), (500, "Medium")):
        css += (f'@font-face{{font-family:M;font-weight:{weight};'
                f'src:url(data:font/ttf;base64,{b64(FONTS / f"MPLUSRounded1c-{name}.ttf")})}}')
    return (f'<!doctype html><meta charset="utf-8"><style>{css}html,body{{margin:0;background:{T["board"] if ground else "transparent"}}}svg{{display:block}}</style>'
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{defs()}'
            + (f'<rect width="{w}" height="{h}" fill="{T["board"]}"/><rect width="{w}" height="{h}" filter="url(#grain)"/>'
               + (stars(w, h) if THEMES[THEME]["night"] else "") if ground else '')
            + f'{body}</svg>')


# ----------------------------------------------------------------------------------------------------- rendering


def find_chrome():
    if os.environ.get("CHROME"):
        return os.environ["CHROME"]
    for p in (r"C:\Program Files\Google\Chrome\Application\chrome.exe", r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
              "/usr/bin/google-chrome", "/usr/bin/chromium", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"):
        if os.path.exists(p):
            return p
    return shutil.which("google-chrome") or shutil.which("chromium") or sys.exit("Chrome not found: set CHROME")


def render(html, w, h, out, scale=2, alpha=False):
    """Chrome draws at 2x, then it is shrunk to w x h: smoother edges than drawing at 1x. The JPEG is written fresh, so it has no metadata."""
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        (td / "p.html").write_text(html, encoding="utf-8")
        subprocess.run([find_chrome(), "--headless", "--disable-gpu", "--hide-scrollbars", f"--user-data-dir={td / 'profile'}",
                        f"--force-device-scale-factor={scale}", f"--window-size={w},{h}", f"--screenshot={td / 'p.png'}",
                        "--virtual-time-budget=4000"] + (["--default-background-color=00000000"] if alpha else []) + [(td / "p.html").as_uri()], check=True, capture_output=True)
        im = Image.open(td / "p.png").convert("RGBA" if alpha else "RGB").crop((0, 0, w * scale, h * scale))
    im = im.resize((w, h), Image.LANCZOS)
    if alpha:
        im.save(out, "PNG", optimize=True)
    else:
        im.save(out, "JPEG", quality=88, optimize=True, progressive=True, subsampling=0)
    print(f"{out.name}: {w}x{h}, {out.stat().st_size // 1024} KB")


# ----------------------------------------------------------------------------------------------------- the pictures


def stars(w, h, n=None, seed=7):
    """Paper night's sky: a few punched stars, some with a little cross."""
    import random
    r = random.Random(seed)
    n = n or max(10, int(w * h / 16000))
    out = []
    for _ in range(n):
        x, y = r.uniform(0, w), r.uniform(0, h)
        k = r.choice([0.8, 1.0, 1.3, 1.7])
        out.append(f'<circle cx="{x:.0f}" cy="{y:.0f}" r="{k}" fill="#F3EEDA" opacity="{r.uniform(.3, .85):.2f}"/>')
        if k > 1.5:
            out.append(f'<path d="M{x:.0f} {y - 5:.0f}V{y + 5:.0f}M{x - 5:.0f} {y:.0f}H{x + 5:.0f}" stroke="#F3EEDA" stroke-width=".8" opacity=".5"/>')
    return "".join(out)


def moon(cx, cy, r):
    return (f'<g filter="url(#printshadow)"><circle cx="{cx}" cy="{cy}" r="{r * 1.9}" fill="#F3EEDA" opacity=".05"/>'
            f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#EDE2BE"/><circle cx="{cx - r * .3}" cy="{cy - r * .25}" r="{r * .2}" fill="#D9CCA2"/>'
            f'<circle cx="{cx + r * .35}" cy="{cy + r * .3}" r="{r * .13}" fill="#D9CCA2"/></g>')


GITHUB_MARK = ("M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z")  # Octicons mark-github (MIT)


def maker_mark(x, y, rot=-2):
    """The maker's hanko: a small ink-blue seal with the GitHub mark in it and the handle beside it, pressed like the stamps."""
    return (f'<g transform="translate({x} {y}) rotate({rot})" filter="url(#ink)" opacity=".78">'
            f'<circle r="31" fill="none" stroke="{INKBLUE}" stroke-width="2.8"/><circle r="25" fill="none" stroke="{INKBLUE}" stroke-width="1.2"/>'
            f'<g transform="translate(-13 -13) scale(1.625)"><path d="{GITHUB_MARK}" fill="{INKBLUE}"/></g>'
            f'<text x="46" y="2" dominant-baseline="central" font-family="M" font-weight="700" font-size="30" letter-spacing="1.5" fill="{INKBLUE}">Lii-lac</text></g>')


def hero():
    W, H = 1280, 640
    back, _ = print_(1002, 292, 500, 5, shot("home-light") if THEME == "dark" else SHOTS / "home-dark.jpg")
    front, _ = print_(912, 348, 580, -1.8, shot("home"), deep=True)
    body = ((moon(150, 96, 38) if THEME == "dark" else "") + back + tape(1090, 148, 120, 36, -14, TAPE["lilac"], "grid")
            + front + tape(642, 188, 124, 38, -36, TAPE["blue"], "dots") + tape(1176, 180, 124, 38, 32, TAPE["pink"], "stripes")
            + title_strip(74, 150, 470, 150, -2, "Washi", 112)
            + f'<g transform="rotate(-1 300 420)" font-family="M" font-weight="700" font-size="44" fill="{T["ink"]}">'
              f'<text x="80" y="388">Everyone’s own saves</text><text x="80" y="446">on one shared Switch.</text></g>'
              + maker_mark(112, 566))
    render(page(W, H, body), W, H, OUT / "hero.jpg")


def strip():
    W, H = 1280, 420
    body = ""
    for cx, rot, shot_, label, tcol, pat, trot in ((215, -1.4, "home", "Home", "green", "stripes", -8),
                                                   (640, 0.9, "swap", "Swap", "pink", "grid", 6),
                                                   (1065, -0.8, "backups", "Backups", "blue", "dots", -5)):
        pr, ph = print_(cx, 196, 384, rot, shot(shot_))
        body += pr + tape(cx, 196 - ph / 2 + 4, 104, 34, trot * 3, TAPE[tcol], pat)
        body += tape(cx, 196 + ph / 2 + 28, 190, 52, rot * 0.5, TAPE[tcol], pat, text=True)
        body += (f'<text x="{cx}" y="{196 + ph / 2 + 29}" text-anchor="middle" dominant-baseline="central" font-family="M" '
                 f'font-weight="700" font-size="30" fill="{T["tx"]}" transform="rotate({rot * 0.5} {cx} {196 + ph / 2 + 28})">{label}</text>')
    render(page(W, H, body), W, H, OUT / "strip.jpg")


# ----------------------------------------------------------------------------------------------------- diagram parts

BUTTON = dict(A="#D4FAC7", B="#FFE3CD", X="#E6F7DD", Y="#FFFF9A", plus="#FFCCFA", minus="#AED6F9")


def txt(x, y, s, size=24, weight=500, fill=None, anchor="start", extra=""):
    fill = fill or T["ink"]
    return (f'<text x="{x}" y="{y}" font-family="M" font-weight="{weight}" font-size="{size}" fill="{fill}" '
            f'text-anchor="{anchor}" {extra}>{s}</text>')


def lines(x, y, rows, size=24, weight=500, fill=None, lh=1.32, anchor="start"):
    return "".join(txt(x, y + i * size * lh, r, size, weight, fill or T["soft"], anchor) for i, r in enumerate(rows))


def slip(cx, cy, w, h, rot, inner="", fill=None, deep=False):
    """A piece of paper: matte, tight shadow, slightly turned. `inner` is drawn in its own centred coordinates."""
    return (f'<g transform="translate({cx} {cy}) rotate({rot})" filter="url(#{"deepshadow" if deep else "printshadow"})">'
            f'<rect x="{-w / 2}" y="{-h / 2}" width="{w}" height="{h}" rx="6" fill="{fill or T["paper"]}"/>{inner}</g>')


def stamp(cx, cy, text, rot=-8, size=30, w=None):
    """Ink-blue rubber stamp, worn like real ink. Means "this one" and nothing else."""
    w = w or len(text) * size * 0.86 + 34
    h = size * 1.55
    return (f'<g transform="translate({cx} {cy}) rotate({rot})" filter="url(#ink)" opacity=".92">'
            f'<rect x="{-w / 2}" y="{-h / 2}" width="{w}" height="{h}" rx="4" fill="none" stroke="{INKBLUE}" stroke-width="3.2"/>'
            f'<rect x="{-w / 2 + 6}" y="{-h / 2 + 6}" width="{w - 12}" height="{h - 12}" rx="2" fill="none" stroke="{INKBLUE}" stroke-width="1.4"/>'
            f'<text x="0" y="1" text-anchor="middle" dominant-baseline="central" font-family="M" font-weight="700" font-size="{size}" '
            f'letter-spacing="{size * 0.14:.1f}" fill="{INKBLUE}">{text}</text></g>')


def avatar(cx, cy, name, r=37):
    uid = f"av-{name}-{int(cx)}-{int(cy)}"
    return (f'<clipPath id="{uid}"><circle cx="{cx}" cy="{cy}" r="{r}"/></clipPath>'
            f'<image href="data:image/jpeg;base64,{b64(shot(f"avatar-{name}"))}" x="{cx - 40}" y="{cy - 40}" width="80" height="80" clip-path="url(#{uid})"/>')


def arrow(x1, y1, x2, y2, bend=0, label=None):
    """A dashed thread with an open arrowhead, bent a little the way a hand would draw it."""
    import math
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2 - bend
    dx, dy = x2 - mx, y2 - my
    a = math.atan2(dy, dx)
    h1 = (x2 - 15 * math.cos(a - 0.5), y2 - 15 * math.sin(a - 0.5))
    h2 = (x2 - 15 * math.cos(a + 0.5), y2 - 15 * math.sin(a + 0.5))
    return (f'<g fill="none" stroke="{T["thread"]}" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round">'
            f'<path d="M{x1} {y1} Q{mx} {my} {x2 - 4 * math.cos(a)} {y2 - 4 * math.sin(a)}" stroke-dasharray="2 9"/>'
            f'<path d="M{h1[0]:.1f} {h1[1]:.1f} L{x2} {y2} L{h2[0]:.1f} {h2[1]:.1f}"/></g>')


def button(cx, cy, letter, r=19):
    col = BUTTON.get(letter, "#fff")
    shown = {"plus": "+", "minus": "−"}.get(letter, letter)
    return (f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{col}" stroke="{T["button"]}" stroke-width="2.4"/>'
            f'<text x="{cx}" y="{cy + 1}" text-anchor="middle" dominant-baseline="central" font-family="M" font-weight="700" '
            f'font-size="{r * 1.0:.0f}" fill="{T["button"]}">{shown}</text>')


def numeral(cx, cy, n):
    """A crayon-drawn ring round the step number."""
    return (f'<circle cx="{cx}" cy="{cy}" r="21" fill="none" stroke="{INKBLUE}" stroke-width="3" stroke-dasharray="120 14" transform="rotate(-20 {cx} {cy})"/>'
            f'<circle cx="{cx}" cy="{cy}" r="24" fill="none" stroke="{INKBLUE}" stroke-opacity=".45" stroke-width="1.6" stroke-dasharray="90 30"/>'
            f'<text x="{cx}" y="{cy + 1}" text-anchor="middle" dominant-baseline="central" font-family="M" font-weight="700" font-size="26" fill="{INKBLUE}">{n}</text>')


def icon_battery(x, y):
    return (f'<g transform="translate({x} {y})" fill="none" stroke="{T["ink"]}" stroke-width="3"><rect width="50" height="26" rx="5"/>'
            f'<rect x="51" y="8" width="5" height="10" rx="1.5" fill="{T["ink"]}"/><rect x="5" y="5" width="22" height="16" rx="2" fill="{TAPE["green"]}" stroke="none"/></g>')


def icon_sd(x, y):
    return (f'<g transform="translate({x} {y})"><path d="M0 8Q0 0 8 0H34L48 14V56Q48 64 40 64H8Q0 64 0 56Z" fill="{TAPE["blue"]}" stroke="{T["ink"]}" stroke-width="3" stroke-linejoin="round"/>'
            + "".join(f'<rect x="{9 + i * 8}" y="6" width="4" height="14" rx="1" fill="{T["dark"]}" fill-opacity=".55"/>' for i in range(4))
            + f'<text x="24" y="46" text-anchor="middle" font-family="M" font-weight="700" font-size="17" fill="{T["dark"]}">SD</text></g>')


def icon_folder(x, y, color):
    return (f'<g transform="translate({x} {y})"><path d="M0 6Q0 0 6 0H18L24 7H40Q46 7 46 13V34Q46 40 40 40H6Q0 40 0 34Z" fill="{color}" stroke="{T["ink"]}" stroke-width="2.6" stroke-linejoin="round"/></g>')


def icon_file(x, y):
    return (f'<g transform="translate({x} {y})"><path d="M0 4Q0 0 4 0H22L34 12V40Q34 44 30 44H4Q0 44 0 40Z" fill="{T["paper"]}" stroke="{T["ink"]}" stroke-width="2.6" stroke-linejoin="round"/>'
            f'<path d="M22 0V12H34" fill="{TAPE["peach"]}" stroke="{T["ink"]}" stroke-width="2.6" stroke-linejoin="round"/></g>')


# ----------------------------------------------------------------------------------------------------- diagrams


def swap_diagram():
    W, H = 1280, 540
    b = ""
    cards = (
        (210, -1.0, "Check", "blue", "dots", None,
         ["Battery at 15% or more,", "or on the charger.", "Room on the SD card."], None),
        (640, 0.8, "Back up Eli", "pink", "stripes", "eli",
         ["Copy every save,", "then compare the copy", "with the original."], ("CHECKED", 26, -8)),
        (1070, -0.7, "Load Sam", "green", "grid", "sam",
         ["Put Sam’s saves on", "the console, then", "compare them again."], ("LOADED", 25, 6)),
    )
    for i, (cx, rot, title, tc, tp, who, rows, st) in enumerate(cards):
        inner = numeral(-124, -112, i + 1) + txt(-88, -98, title, 36, 700)
        if who:
            inner += f'<g transform="translate({-112} {-26})">{avatar(0, 0, who, 36)}</g>'
        else:
            inner += f'<g transform="translate(-142 -50)">{icon_battery(0, 0)}</g><g transform="translate(-40 -62)">{icon_sd(0, 0)}</g>'
        inner += lines(-152, 40, rows, 24, 500)
        if st:
            inner += stamp(70, 148, st[0], st[2], st[1])
        b += slip(cx, 250, 350, 400, rot, inner, deep=(i == 1))
        b += tape(cx + 4, 250 - 200, 120, 36, [-8, 7, -5][i], TAPE[tc], tp)
    b += arrow(392, 270, 452, 270, 16) + arrow(822, 270, 882, 270, 16)
    inner = txt(0, 10, "Power cut halfway? The next time Washi starts, it offers to finish the swap.", 24, 500, T["soft"], "middle")
    b += slip(640, 500, 900, 56, -0.4, inner)
    return page(W, H, b), W, H


def sd_diagram():
    W, H = 1280, 400
    b = ""
    left = [("file", "switch/washi.nro", "the app: you copy this", None),
            ("folder", "atmosphere/contents/4200000000534C4B/", "optional: the sign-out reminder", None)]
    right = [("folder", "switch/washi/", "profiles, settings, log", "pink"),
             ("folder", "Washi Backups/", "copies of everything", "yellow")]
    for cx, rot, title, tc, rows in ((260, -0.8, "You put on the card", "green", left), (1020, 0.7, "Washi makes", "blue", right)):
        inner = txt(-208, -100, title, 32, 700)
        for i, (kind, name, note, col) in enumerate(rows):
            y = -56 + i * 100
            inner += (icon_file(-208, y) if kind == "file" else icon_folder(-208, y + 4, TAPE[col or "peach"]))
            if len(name) >= 30:
                head, tail = name.split("contents/")
                inner += txt(-150, y + 10, head + "contents/", 21, 700, T["ink"]) + txt(-150, y + 36, tail, 21, 700, T["ink"]) + txt(-150, y + 66, note, 20, 500, T["soft"])
            else:
                inner += txt(-150, y + 18, name, 22, 700, T["ink"]) + txt(-150, y + 50, note, 20, 500, T["soft"])
        b += slip(cx, 205, 480, 290, rot, inner) + tape(cx + (190 if cx < 600 else 130), 205 - 145, 110, 36, [-6, 6][cx > 600], TAPE[tc], ("grid", "chevron")[cx > 600])
    b += f'<g transform="translate(616 170) rotate(-3)" filter="url(#printshadow)">{icon_sd(0, 0)}</g>'
    b += txt(640, 276, "SD card", 24, 700, T["ink"], "middle")
    b += arrow(512, 205, 590, 205, 16) + arrow(768, 205, 692, 205, 16)
    return page(W, H, b), W, H


def import_shots(src):
    """PNGs from shots.sh -> JPEGs. Light theme: home, home-dark (the light page's second print), swap, backups. Dark theme: home-dark, swap-dark, backups-dark."""
    src = pathlib.Path(src)
    (SHOTS / "dark").mkdir(parents=True, exist_ok=True)
    jobs = (("home", SHOTS / "home.jpg"), ("home-dark", SHOTS / "home-dark.jpg"), ("swap", SHOTS / "swap.jpg"), ("backups", SHOTS / "backups.jpg"),
            ("home-dark", SHOTS / "dark" / "home.jpg"), ("swap-dark", SHOTS / "dark" / "swap.jpg"), ("backups-dark", SHOTS / "dark" / "backups.jpg"),
            ("home", SHOTS / "dark" / "home-light.jpg"))
    for name, out in jobs:
        Image.open(src / f"{name}.png").convert("RGB").resize((960, 540), Image.LANCZOS).save(out, "JPEG", quality=88, optimize=True, progressive=True)
        print(out.relative_to(HERE))
    # the people in the diagrams: each tile's picture and its ring, cut from the home screen of each theme
    for png, folder in (("home", SHOTS), ("home-dark", SHOTS / "dark")):
        home = Image.open(src / f"{png}.png").convert("RGB")
        for name, (x, y) in dict(ava=(115, 381), eli=(279, 381), sam=(115, 543), uma=(443, 551)).items():
            home.crop((x - 40, y - 40, x + 40, y + 40)).save(folder / f"avatar-{name}.jpg", "JPEG", quality=92)
            print((folder / f"avatar-{name}.jpg").relative_to(HERE))


# ----------------------------------------------------------------------------------------------------- the README's own pieces

ORDER = ("pink", "peach", "yellow", "green", "mint", "blue", "lilac", "rose")
PATS = ("stripes", "dots", "grid", "chevron")


def crayon_col(name):
    c = TAPE[name]
    return mix(c, "#FFFFFF", .22) if THEMES[THEME]["night"] else mix(c, "#2D2D2D", .38)


# ------------------------------------------------------------------ crayon doodles, drawn around (0,0), about 44 px across
DOODLES = dict(
    star="M0 -22L6 -7L22 -6L9.5 4L14 20L0 11L-14 20L-9.5 4L-22 -6L-6 -7Z",
    heart="M0 18C-30 -2 -18 -26 0 -10C18 -26 30 -2 0 18Z",
    check="M-18 2L-6 15L20 -16",
    squiggle="M-26 0Q-18 -14 -10 0T6 0T22 0",
    sparkle="M0 -22Q2 -2 22 0Q2 2 0 22Q-2 2 -22 0Q-2 -2 0 -22Z",
    bolt="M6 -22L-12 4H-1L-6 22L14 -6H3Z",
    lock="M-14 -2H14V20H-14Z M-9 -2V-10A9 9 0 0 1 9 -10V-2",
    smile="M-20 0A20 20 0 1 0 20 0A20 20 0 1 0 -20 0 M-8 -6V-3 M8 -6V-3 M-10 6Q0 15 10 6",
    flower="M0 -6C-6 -22 6 -22 0 -6 M6 -2C22 -8 22 4 6 -2 M3 6C14 20 2 24 3 6 M-3 6C-14 20 -2 24 -3 6 M-6 -2C-22 -8 -22 4 -6 -2 M-4 0A4 4 0 1 0 4 0A4 4 0 1 0 -4 0",
    note="M-8 14V-14L14 -20V8 M-8 14A6 6 0 1 1 -8 14.1 M14 8A6 6 0 1 1 14 8.1",
    arrow="M-22 4Q0 -10 20 0 M10 -12L22 0L8 10",
    info="M-20 0A20 20 0 1 0 20 0A20 20 0 1 0 -20 0 M0 -4V10 M0 -12V-11",
    page="M-14 -20H8L16 -12V20H-14Z M-8 -4H8 M-8 4H8 M-8 12H4",
)


def doodle(name, x, y, rot=0, color="#000", scale=1.0, sw=3.4):
    return (f'<g transform="translate({x} {y}) rotate({rot}) scale({scale})" filter="url(#ink)"><rect x="-34" y="-34" width="68" height="68" fill="none"/>'
            f'<path d="{DOODLES[name]}" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round"/></g>')


# ------------------------------------------------------------------ dividers (transparent, 1280 wide)
def div_sampler():
    W, H = 1280, 90
    r = random.Random(4)
    body = ""
    n = 8
    for i in range(n):
        cx = 100 + i * (W - 200) / (n - 1) + r.uniform(-8, 8)
        body += tape(cx, 45 + r.uniform(-7, 7), r.uniform(150, 185), 40, r.uniform(-7, 7), TAPE[ORDER[i % 8]], PATS[(i * 3 + 1) % 4])
    return page(W, H, body, ground=False), W, H


def div_doodles():
    W, H = 1280, 96
    thread = f'<path d="M30 52Q640 62 1250 52" fill="none" stroke="{T["thread"]}" stroke-width="3" stroke-linecap="round" stroke-dasharray="2 10"/>'
    body = thread + tape(34, 52, 62, 30, -28, TAPE["blue"], "dots") + tape(1246, 52, 62, 30, 28, TAPE["pink"], "stripes")
    names = ("heart", "star", "squiggle", "flower", "sparkle", "note", "star", "heart", "sparkle", "flower", "squiggle", "star")
    cols = ("pink", "yellow", "blue", "rose", "mint", "lilac", "peach", "green")
    r = random.Random(9)
    for i, nme in enumerate(names):
        x = 110 + i * (W - 220) / (len(names) - 1)
        body += doodle(nme, x, 54 + r.uniform(-6, 6), r.uniform(-14, 14), crayon_col(cols[i % 8]), scale=r.uniform(.8, 1.05))
    return page(W, H, body, ground=False), W, H


def div_bunting():
    W, H = 1280, 128
    p0, p1, p2 = (24, 16), (640, 76), (1256, 16)
    bez = lambda t: ((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0], (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1])
    body = (f'<path d="M{p0[0]} {p0[1]}Q{p1[0]} {p1[1]} {p2[0]} {p2[1]}" fill="none" stroke="{T["thread"]}" stroke-width="2.6" stroke-linecap="round"/>')
    n = 15
    for i in range(n):
        t = .045 + i * .91 / (n - 1)
        x, y = bez(t)
        x2, y2 = bez(t + .01)
        ang = math.degrees(math.atan2(y2 - y, x2 - x))
        col = ORDER[i % 8]
        pat = PATS[i % 4]
        pts = "-23,0 23,0 0,60"
        body += (f'<g transform="translate({x:.1f} {y:.1f}) rotate({ang:.1f})"><g transform="rotate({(-4 if i % 2 else 4)})" filter="url(#printshadow)">'
                 f'<polygon points="{pts}" fill="{TAPE[col]}"/><polygon points="{pts}" fill="url(#p-{pat})"/>'
                 f'<rect x="-14" y="-6" width="28" height="12" rx="1" fill="{TAPE[ORDER[(i + 4) % 8]]}" fill-opacity=".9"/></g></g>')
    body += tape(p0[0] - 2, p0[1], 50, 28, -40, TAPE["green"], "grid") + tape(p2[0] + 2, p2[1], 50, 28, 40, TAPE["peach"], "chevron")
    return page(W, H, body, ground=False), W, H


def div_peel():
    """One strip along the page whose end is lifted off the paper: the flap shares the strip's end edge, so it reads as a single piece of tape."""
    W, H = 1280, 100
    col = TAPE["mint"]
    k = THEMES[THEME]["sh"]
    top, h, fx = 40, 40, 1110          # strip top edge, thickness, x of the fold
    lift, fl = 30, 104                  # how far the end rises, how long the lifted part is
    under = mix(col, "#FFFFFF", .42)  # the sticky side shows lighter
    body_pts = zig_pts(fx + 20, h, left=False, right=False)
    flap_pts = zig_pts(fl, h, left=False, right=True)
    out = (f'<defs><linearGradient id="crease" x1="0" x2="1" y1="0" y2="0"><stop offset="0" stop-color="#000" stop-opacity="{.30 * min(k, 1.4):.2f}"/>'
           f'<stop offset="1" stop-color="#000" stop-opacity="0"/></linearGradient></defs>')
    out += (f'<g filter="url(#tapeshadow)"><g transform="translate(-20 {top})">'
            f'<polygon points="{body_pts}" fill="{col}" fill-opacity=".88"/><polygon points="{body_pts}" fill="url(#p-dots)"/></g></g>')
    out += (f'<defs><filter id="soft" x="-30%" y="-60%" width="160%" height="220%"><feGaussianBlur stdDeviation="4"/></filter></defs>'
            f'<g transform="matrix(1 {-lift / fl:.4f} 0 1 {fx + 4} {top + 14})" filter="url(#soft)"><polygon points="{flap_pts}" fill="{T["shadow"]}" opacity="{.30 * k:.2f}"/></g>')
    out += (f'<g transform="matrix(1 {-lift / fl:.4f} 0 1 {fx} {top})" filter="url(#tapeshadow)">'
            f'<polygon points="{flap_pts}" fill="{under}" fill-opacity=".95"/><polygon points="{flap_pts}" fill="url(#p-dots)" opacity=".3"/>'
            f'<rect x="0" y="0" width="16" height="{h}" fill="url(#crease)"/></g>')
    out += f'<rect x="{fx - 3}" y="{top}" width="3" height="{h}" fill="#000" opacity="{.16 * min(k, 1.4):.2f}"/>'
    return page(W, H, out, ground=False), W, H


# ------------------------------------------------------------------ nav labels with a crayon icon
def text_w(label, size):
    from PIL import ImageFont
    return ImageFont.truetype(str(FONTS / "MPLUSRounded1c-Bold.ttf"), size).getlength(label)


def nav_label(label, color, pat, rot, icon):
    size = 34
    tw_text = text_w(label, size)
    icon_w, gap, pad = 30, 12, 30
    group = icon_w + gap + tw_text
    tw = group + 2 * pad
    W, H = int(tw + 40), 90
    cx, cy = W / 2, H / 2
    left = cx - group / 2
    body = tape(cx, cy, tw, 58, rot, TAPE[color], pat, text=True)
    body += doodle(icon, left + icon_w / 2, cy, rot * 2, T["tx"], scale=.62, sw=4)
    body += (f'<text x="{left + icon_w + gap:.1f}" y="{cy + 1}" dominant-baseline="central" font-family="M" font-weight="700" font-size="{size}" fill="{T["tx"]}" '
             f'transform="rotate({rot} {cx} {cy})">{label}</text>')
    return page(W, H, body, ground=False), W, H


NAV = (("install", "Install", "blue", "dots", -1.2, "arrow"), ("using-it", "Using it", "green", "grid", 1.0, "star"),
       ("good-to-know", "Good to know", "peach", "chevron", -0.8, "info"), ("docs", "Docs", "lilac", "stripes", 1.2, "page"))


# ------------------------------------------------------------------ feature board and key legend (with ground)
def card_text(rows, y0, size, fill, weight=500, lh=1.3):
    return "".join(txt(-108, y0 + i * size * lh, r, size, weight, fill) for i, r in enumerate(rows))


def features():
    W, H = 1280, 380
    cards = (
        ("check", "pink", ("Checked", "backups"), ("Every copy is", "compared with the", "original before", "anything is cleared."), -1.6),
        ("bolt", "yellow", ("Safe if the", "power dies"), ("Swaps are journaled.", "If the power is cut,", "Washi offers to", "finish on next start."), 1.2),
        ("lock", "blue", ("Passwords,", "if you want"), ("Six digits per profile,", "plus one for admin."), -1.0),
        ("smile", "green", ("Real profile", "pictures"), ("Washi reads the", "Switch’s own, plus", "extra character", "icons you download."), 1.8),
    )
    body = ""
    for i, (ic, col, title, lines_, rot) in enumerate(cards):
        cx, cy = 160 + i * 320, 196
        inner = (doodle(ic, -78, -96, -6, crayon_col(col), 1.45, 3.6) + card_text(title, -38, 31, T["ink"], 700, 1.15)
                 + card_text(lines_, 48, 20, T["soft"]))
        body += slip(cx, cy, 270, 300, rot, inner) + tape(cx + (-6 if i % 2 else 8), cy - 150 + 2, 104, 34, rot * 5 + (6 if i % 2 else -5), TAPE[col], PATS[i])
    return page(W, H, body), W, H


def legend():
    W, H = 1280, 300
    tiles = (("A", "Play", "then Switch", "green", ["A"]), ("X", "Sign out", "backs up, clears", "pink", ["X"]),
             ("Y", "Search", "a few letters is enough", "yellow", ["Y"]), ("R3", "Order", "press the right stick in", "blue", ["R3"]),
             ("admin", "Admin", "hold all four", "lilac", ["ZL", "L", "↑", "plus"]))
    body = ""
    for i, (k, word, sub, col, glyphs) in enumerate(tiles):
        cx, cy = 128 + i * 256, 152
        rot = (-1.4, 1.1, -0.8, 1.4, -1.0)[i]
        g = ""
        if len(glyphs) == 1:
            g = button(0, -42, glyphs[0], 32)
        else:
            g = "".join(button(-57 + j * 38, -44, s, 17) for j, s in enumerate(glyphs))
        inner = g + txt(0, 26, word, 32, 700, T["ink"], "middle") + txt(0, 58, sub, 17, 500, T["soft"], "middle")
        body += slip(cx, cy, 226, 206, rot, inner) + tape(cx, cy - 103 + 3, 92, 32, rot * 6, TAPE[col], PATS[i % 4])
    return page(W, H, body), W, H


def roll_divider(theme):
    """The app's own progress roll (source/gfx/paper_roll.c, compiled by roll.sh) laying a straight tape to the right edge, as a transparent PNG:
    drawn on white and on black, then the two are matted into colour + alpha."""
    import numpy as np
    tmp = HERE / ".roll"
    tmp.mkdir(exist_ok=True)
    cmd = ["bash", "docs/readme/roll.sh", "docs/readme/.roll"]
    if os.name == "nt":
        cmd = ["wsl"] + cmd
    subprocess.run(cmd, cwd=HERE.parent.parent, check=True, capture_output=True)
    pre = tmp / ("D" if theme == "dark" else "L")
    w = np.asarray(Image.open(f"{pre}-white.png").convert("RGB")).astype(np.float64)
    k = np.asarray(Image.open(f"{pre}-black.png").convert("RGB")).astype(np.float64)
    a = np.clip(1.0 - (w - k).mean(axis=2) / 255.0, 0, 1)
    col = np.where(a[..., None] > 0.004, k / np.maximum(a[..., None], 0.004), 0)
    im = Image.fromarray(np.dstack([np.clip(col, 0, 255), a * 255]).astype(np.uint8), "RGBA")
    ys = np.where(a.max(axis=1) > 0.02)[0]
    im = im.crop((0, max(0, ys.min() - 4), im.width, min(im.height, ys.max() + 6)))
    im.save(OUT / "div-roll.png", optimize=True)
    print(f"div-roll.png: {im.width}x{im.height}")


DIVS = (("sampler", div_sampler), ("doodles", div_doodles), ("bunting", div_bunting), ("peel", div_peel))


def draw_all(theme):
    set_theme(theme)
    check_contrast()
    hero()
    strip()
    for name, fn in (("swap", swap_diagram), ("sd-card", sd_diagram)):
        html, w, h = fn()
        render(html, w, h, OUT / f"diagram-{name}.jpg")
    html, w, h = features()
    render(html, w, h, OUT / "features.jpg")
    html, w, h = legend()
    render(html, w, h, OUT / "keys.jpg")
    roll_divider(theme)
    for name, fn in DIVS:
        html, w, h = fn()
        render(html, w, h, OUT / f"div-{name}.png", alpha=True)
    for slug, label, color, pat, rot, icon in NAV:
        html, w, h = nav_label(label, color, pat, rot, icon)
        render(html, w, h, OUT / f"nav-{slug}.png", alpha=True)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--import", dest="imp", metavar="DIR", help="folder of PNGs written by shots.sh")
    ap.add_argument("--theme", choices=("light", "dark", "both"), default="both")
    a = ap.parse_args()
    if a.imp:
        import_shots(a.imp)
    for th in (("light", "dark") if a.theme == "both" else (a.theme,)):
        draw_all(th)
    shutil.rmtree(HERE / ".roll", ignore_errors=True)
