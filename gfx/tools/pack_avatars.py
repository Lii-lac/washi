"""Packs profile pictures into one file the app embeds (data/avatars.bin).

    python pack_avatars.py <folder of 256x256 PNGs> <README.md listing them> <out.bin> [size] [--quantize]

The README is the one from the picture collection; its image tags give each picture its number (its id: profiles store this
number, so it never changes). The grid shows them grouped by series (see GROUPS), in the order stored in the pack.
Each picture is stored as a full-colour PNG with transparency at `size` px (default 256, the console's own resolution);
--quantize shrinks it to 256 colours (about a fifth of the size, with visible banding). Needs Pillow.

Every picture is also stored at half the size (a thumbnail): the grids, tiles and lists draw them small, and a small PNG decodes
several times faster than the full one.

File layout (little endian): "SLAV", u32 version=3, u32 count, u32 size, then count * (u32 offset, u32 length) measured from
the start of the file, then count * u16 (picture id for each grid position), then count * (u32 offset, u32 length) for the
thumbnails, then the PNG bytes (the pictures, then the thumbnails).
"""
import io, os, re, struct, sys

from PIL import Image

# Which series each picture belongs to, by number (the order of the README). Pictures are listed series by series, and inside a
# series in their original order. Sorted out by looking at every picture.
GROUPS = [
    ('Mario', list(range(0, 18)) + list(range(66, 72)) + list(range(102, 106)) + list(range(108, 114)) + list(range(123, 126)) + list(range(135, 147))),
    ('The Legend of Zelda', list(range(18, 30)) + list(range(72, 78))),
    ('Animal Crossing', list(range(30, 42)) + list(range(129, 135)) + [147]),
    ('Splatoon', list(range(42, 51)) + list(range(60, 66)) + list(range(99, 102)) + list(range(114, 123))),
    ('ARMS', list(range(78, 93))),
    ('Kirby', list(range(93, 99))),
    ('Pikmin', [51, 52, 53]),
    ('Star Fox', [54, 55, 56]),
    ('Metroid', [57, 58, 59]),
    ('Nintendo Labo', [126, 127, 128]),
    ('Other', [106, 107]),
]


def display_order(count):
    order = [i for _, ids in GROUPS for i in sorted(ids)]
    assert sorted(order) == list(range(count)), 'every picture must be in exactly one group'
    return order


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    quantize = '--quantize' in sys.argv
    src, readme, out = args[:3]
    size = int(args[3]) if len(args) > 3 else 256
    names = []
    for n in re.findall(r'avatars/([0-9a-f]{64})\.png', open(readme, encoding='utf-8').read()):
        if n not in names:
            names.append(n)
    order = display_order(len(names))
    blobs, thumbs = [], []
    for n in names:
        im = Image.open(os.path.join(src, n + '.png')).convert('RGBA')
        if im.size != (size, size):
            im = im.resize((size, size), Image.LANCZOS)
        small = im.resize((size // 2, size // 2), Image.LANCZOS)
        if quantize:
            small = small.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
            im = im.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
        buf = io.BytesIO()
        im.save(buf, 'PNG', optimize=True)
        blobs.append(buf.getvalue())
        buf = io.BytesIO()
        small.save(buf, 'PNG', optimize=True)
        thumbs.append(buf.getvalue())
    head = 16 + 8 * len(blobs) + 2 * len(blobs) + 8 * len(blobs)
    table, ttable, off = b'', b'', head
    for b in blobs:
        table += struct.pack('<II', off, len(b))
        off += len(b)
    for b in thumbs:
        ttable += struct.pack('<II', off, len(b))
        off += len(b)
    with open(out, 'wb') as f:
        f.write(b'SLAV' + struct.pack('<III', 3, len(blobs), size) + table + b''.join(struct.pack('<H', i) for i in order) + ttable + b''.join(blobs) + b''.join(thumbs))
    print(len(blobs), 'pictures,', off // 1024, 'KB ->', out)


main()
