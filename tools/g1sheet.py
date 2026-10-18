"""Contact sheet of RCT sprites from the user's game folder (debugging aid, not part of the build).

usage: python tools/g1sheet.py <game dir> <first id> <count> [zoom] [out.png]   (default build/g1_<first>.png)
Each cell shows a sprite (scaled by zoom, default 1) with its id. Reads Data/CSG1.DAT and csg1i.dat (see docs/launcher.md);
nothing from the game is written anywhere except the PNG in build/.
"""
import os
import struct
import sys

from PIL import Image, ImageDraw

PALETTE_ID = 2024


def open_g1(game):
    for idx in (r"Data\CSG1I.DAT", r"RCTdeluxe_install\Data\csg1i.dat"):
        if os.path.exists(os.path.join(game, idx)):
            break
    raw = open(os.path.join(game, idx), "rb").read()
    entries = [struct.unpack_from("<IhhhhHH", raw, i) for i in range(0, len(raw), 16)]
    return entries, open(os.path.join(game, r"Data\CSG1.DAT"), "rb").read()


def decode(e, data):
    """Returns (w, h, bytearray of palette indices, 0 = transparent)."""
    off, w, h, _, _, flags, _ = e
    if w <= 0 or h <= 0 or flags & 8:
        return w, h, None
    if not flags & 4:
        return w, h, bytearray(data[off:off + w * h])
    px = bytearray(w * h)
    for y in range(h):
        p = off + struct.unpack_from("<H", data, off + 2 * y)[0]
        while True:
            n, x = data[p], data[p + 1]
            run = n & 0x7F
            px[y * w + x:y * w + x + run] = data[p + 2:p + 2 + run]
            p += 2 + run
            if n & 0x80:
                break
    return w, h, px


def palette(entries, data):
    off, count, _, first = entries[PALETTE_ID][:4]
    pal = [(255, 255, 255)] * 256   # indices the game remaps at draw time (text colour) show white
    for i in range(count):
        b, g, r = data[off + 3 * i:off + 3 * i + 3]
        pal[first + i] = (r, g, b)
    return pal


def main(game, first, count, zoom=1, out=None):
    first, count, zoom = int(first), int(count), int(zoom)
    entries, data = open_g1(game)
    pal = palette(entries, data)
    cells = []
    for i in range(first, min(first + count, len(entries))):
        w, h, px = decode(entries[i], data)
        if px is None:
            continue
        img = Image.new("RGB", (w, h), (255, 0, 255))
        img.putdata([pal[c] if c else (255, 0, 255) for c in px])
        cells.append((i, img.resize((w * zoom, h * zoom), Image.NEAREST)))
    cw = max([64] + [min(c.width, 640) for _, c in cells]) + 4
    cols = max(1, 1600 // cw)
    rows, y, row, rh = [], 0, [], 0
    for i, c in cells:
        row.append((i, c))
        rh = max(rh, min(c.height, 480) + 14)
        if len(row) == cols:
            rows.append((row, rh))
            row, rh = [], 0
    if row:
        rows.append((row, rh))
    sheet = Image.new("RGB", (cols * cw, sum(h for _, h in rows) or 1), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for r, h in rows:
        for k, (i, c) in enumerate(r):
            sheet.paste(c, (k * cw + 2, y + 12))
            d.text((k * cw + 2, y), str(i), fill=(255, 255, 0))
        y += h
    out = out or f"build/g1_{first}.png"
    sheet.save(out)
    print(out, len(cells), "sprites")


if __name__ == "__main__":
    main(*sys.argv[1:])
