"""Turns the club's QR code (an SVG of rounded white modules, made with a QR generator) into data/qr.bin: a white picture whose
transparency is the code, so the app can paint it in any colour it likes (dark on a light tile, which every phone reads).

    python make_qr.py nuesports_qr.svg ../../data/qr.bin [pixels per module, default 16]

Only what the generator writes is understood: paths made of M m L l H h V v A a Z z with circular arcs, filled "evenodd" or not.
Drawn four times larger and averaged down, so the edges are smooth. Needs Pillow.
"""
import math, re, sys
from PIL import Image, ImageChops, ImageDraw

SS = 4  # supersampling


def tokens(d):
    for m in re.finditer(r'([MmLlHhVvAaZz])|(-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)', d):
        yield m.group(1) or float(m.group(2))


def arc_points(x0, y0, rx, ry, large, sweep, x1, y1, steps=24):
    # endpoint to centre form (SVG spec F.6.5) for a circular arc
    if rx == 0 or ry == 0:
        return [(x1, y1)]
    dx, dy = (x0 - x1) / 2, (y0 - y1) / 2
    r = rx
    lam = (dx * dx + dy * dy) / (r * r)
    if lam > 1:
        r *= math.sqrt(lam)
    num = r * r * r * r - r * r * (dx * dx + dy * dy)
    den = r * r * (dx * dx + dy * dy)
    co = math.sqrt(max(num / den, 0)) if den else 0
    if large == sweep:
        co = -co
    cxp, cyp = co * dy, -co * dx
    cx, cy = cxp + (x0 + x1) / 2, cyp + (y0 + y1) / 2
    a0 = math.atan2((dy - cyp) / r, (dx - cxp) / r)
    a1 = math.atan2((-dy - cyp) / r, (-dx - cxp) / r)
    da = a1 - a0
    if sweep and da < 0:
        da += 2 * math.pi
    if not sweep and da > 0:
        da -= 2 * math.pi
    return [(cx + r * math.cos(a0 + da * i / steps), cy + r * math.sin(a0 + da * i / steps)) for i in range(1, steps + 1)]


def subpaths(d):
    toks = list(tokens(d))
    i, cmd, x, y, sx, sy = 0, None, 0.0, 0.0, 0.0, 0.0
    out, cur = [], None
    nargs = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'A': 7, 'Z': 0}
    while i < len(toks):
        if isinstance(toks[i], str):
            cmd = toks[i]
            i += 1
            if cmd in 'Zz':
                if cur:
                    out.append(cur)
                cur, x, y = None, sx, sy
                continue
        up, rel = cmd.upper(), cmd.islower()
        a = toks[i:i + nargs[up]]
        i += nargs[up]
        if up == 'M':
            if cur:
                out.append(cur)
            x, y = (x + a[0], y + a[1]) if rel else (a[0], a[1])
            sx, sy = x, y
            cur = [(x, y)]
            cmd = 'l' if rel else 'L'  # further pairs are lines
        elif up == 'L':
            x, y = (x + a[0], y + a[1]) if rel else (a[0], a[1])
            cur.append((x, y))
        elif up == 'H':
            x = x + a[0] if rel else a[0]
            cur.append((x, y))
        elif up == 'V':
            y = y + a[0] if rel else a[0]
            cur.append((x, y))
        elif up == 'A':
            nx, ny = (x + a[5], y + a[6]) if rel else (a[5], a[6])
            cur.extend(arc_points(x, y, a[0], a[1], int(a[3]), int(a[4]), nx, ny))
            x, y = nx, ny
    if cur:
        out.append(cur)
    return out


def main():
    src, out = sys.argv[1], sys.argv[2]
    per = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    svg = open(src, encoding='utf-8').read()
    vb = [float(v) for v in re.search(r'viewBox="([^"]+)"', svg).group(1).split()]
    scale = per * SS
    size = (int(vb[2] * scale), int(vb[3] * scale))
    total = Image.new('L', size, 0)
    for m in re.finditer(r'<path ([^>]*?)d="([^"]+)"', svg):
        evenodd = 'evenodd' in m.group(1)
        layer = Image.new('L', size, 0)
        for poly in subpaths(m.group(2)):
            if len(poly) < 3:
                continue
            one = Image.new('L', size, 0)
            ImageDraw.Draw(one).polygon([((px - vb[0]) * scale, (py - vb[1]) * scale) for px, py in poly], fill=255)
            layer = ImageChops.logical_xor(layer.convert('1'), one.convert('1')).convert('L') if evenodd else ImageChops.lighter(layer, one)
        total = ImageChops.lighter(total, layer)
    small = total.resize((size[0] // SS, size[1] // SS), Image.BOX)
    rgba = Image.new('RGBA', small.size, (255, 255, 255, 0))
    rgba.putalpha(small)
    rgba.save(out, format='PNG', optimize=True)
    print(rgba.size, '->', out)


main()
