#!/usr/bin/env python3
"""An icon (.info) from pixel art drawn large in a PNG: the art is reduced to
a small palette and each icon pixel takes the colour that covers most of its
area, so edges stay crisp even when the drawing's own pixel grid is uneven.
The icon has a 4-colour image for any Workbench and an OS 3.5+ colour image
(see mkicon.py, which writes both).

usage: png2icon.py SRC.png OUT.info --height N [--crop X0,Y0,X1,Y1]
                   [--kind tool|project] [--tooltypes] [--colours K] [--preview P.png]

--crop    the part of SRC to use (source pixels, inclusive); default: all
          pixels that are not transparent
--height  the icon's height; the width follows the crop's shape
--tooltypes  the program icon's tooltypes (mkicon.TOOLTYPES)
SimpleDock - Copyright (C) 2026 renewable-intel, GPL-3.0-or-later"""
import argparse, os, random, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkicon as mk


def read_png(path):
    """8-bit RGBA, not interlaced (what drawing programs write)."""
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit(f'{path}: not a PNG')
    i, idat, w, h = 8, b'', 0, 0
    while i < len(d):
        n, = struct.unpack('>I', d[i:i + 4])
        t, b = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        i += 12 + n
        if t == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', b)
            if depth != 8 or ctype != 6 or inter:
                raise SystemExit(f'{path}: needs 8-bit RGBA, not interlaced')
        elif t == b'IDAT':
            idat += b
    raw, stride = zlib.decompress(idat), w * 4
    out, prev, p = bytearray(h * stride), bytearray(stride), 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, out


def opaque_box(w, h, px):
    xs = [x for y in range(0, h, 2) for x in range(0, w, 2) if px[(y * w + x) * 4 + 3] > 128]
    ys = [y for y in range(0, h, 2) for x in range(0, w, 2) if px[(y * w + x) * 4 + 3] > 128]
    return min(xs), min(ys), max(xs), max(ys)


def palette(w, px, box, k, merge=22):
    """k colours by k-means over a sample of the opaque pixels; colours
    closer than 'merge' are joined (near-equal greys give speckles)."""
    x0, y0, x1, y1 = box
    random.seed(1)
    pts = []
    for _ in range(40000):
        x, y = random.randint(x0, x1), random.randint(y0, y1)
        i = (y * w + x) * 4
        if px[i + 3] > 200:
            pts.append((px[i], px[i + 1], px[i + 2]))
    counts = {}
    for r, g, b in pts:
        counts[(r >> 4, g >> 4, b >> 4)] = counts.get((r >> 4, g >> 4, b >> 4), 0) + 1
    cent = []
    for (r, g, b), _ in sorted(counts.items(), key=lambda e: -e[1]):
        c = (r * 16 + 8, g * 16 + 8, b * 16 + 8)
        if all(sum((c[j] - e[j]) ** 2 for j in range(3)) > 40 * 40 for e in cent):
            cent.append(c)
        if len(cent) == k:
            break
    for _ in range(12):
        acc = [[0, 0, 0, 0] for _ in cent]
        for p in pts:
            j = min(range(len(cent)), key=lambda j: sum((p[t] - cent[j][t]) ** 2 for t in range(3)))
            a = acc[j]
            a[0] += p[0]; a[1] += p[1]; a[2] += p[2]; a[3] += 1
        cent = [(a[0] // a[3], a[1] // a[3], a[2] // a[3]) if a[3] else cent[j] for j, a in enumerate(acc)]
    kept = []
    for c in cent:
        if all(sum((c[j] - e[j]) ** 2 for j in range(3)) > merge * merge for e in kept):
            kept.append(c)
    return kept


def resample(w, px, box, tw, th, pal):
    """Each icon pixel: the palette colour most of its cell's inner part has
    (its edges are soft in a drawn image), or transparent."""
    x0, y0, x1, y1 = box
    sw, sh = (x1 - x0 + 1) / tw, (y1 - y0 + 1) / th
    cache, out = {}, []

    def near(c):
        if c not in cache:
            cache[c] = min(range(len(pal)), key=lambda j: sum((c[t] - pal[j][t]) ** 2 for t in range(3)))
        return cache[c]

    for ty in range(th):
        for tx in range(tw):
            votes, solid, n = {}, 0, 0
            for sy in range(5):
                for sx in range(5):
                    x = int(x0 + (tx + 0.2 + 0.15 * sx) * sw)
                    y = int(y0 + (ty + 0.2 + 0.15 * sy) * sh)
                    i = (y * w + x) * 4
                    n += 1
                    if px[i + 3] >= 128:
                        solid += 1
                        j = near((px[i] & 0xFC, px[i + 1] & 0xFC, px[i + 2] & 0xFC))
                        votes[j] = votes.get(j, 0) + 1
            out.append(pal[max(votes, key=votes.get)] + (1.0,) if solid * 2 >= n else (0, 0, 0, 0.0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('out')
    ap.add_argument('--height', type=int, required=True)
    ap.add_argument('--crop')
    ap.add_argument('--kind', choices=('tool', 'project'), default='tool')
    ap.add_argument('--tooltypes', action='store_true')
    ap.add_argument('--colours', type=int, default=20)
    ap.add_argument('--preview')
    a = ap.parse_args()
    w, h, px = read_png(a.src)
    box = tuple(int(v) for v in a.crop.split(',')) if a.crop else opaque_box(w, h, px)
    th = a.height
    tw = round((box[2] - box[0] + 1) / (box[3] - box[1] + 1) * th)
    if not (1 <= tw <= 256 and 1 <= th <= 256):
        raise SystemExit(f'{tw} x {th}: colour icons are at most 256 x 256')
    pal = palette(w, px, box, a.colours)
    normal = resample(w, px, box, tw, th, pal)
    selected = [tuple(int(v * 0.7) for v in p[:3]) + (p[3],) for p in normal]   # as Workbench shows a click
    mk.W, mk.H = tw, th
    kind = mk.WBTOOL if a.kind == 'tool' else mk.WBPROJECT
    data = mk.diskobject(normal, selected, mk.TOOLTYPES if a.tooltypes else [], kind, None) + \
        mk.colour_icon([mk.quantise(normal), mk.quantise(selected)])
    open(a.out, 'wb').write(data)
    print(f'{a.out}: {tw} x {th}, {len(pal)} colours, {len(data)} bytes')
    if a.preview:                                # 6x, normal and selected side by side
        z, rgba = 6, []
        for y in range(th * z):
            for img in (normal, selected):
                for x in range(tw * z):
                    r, g, b, al = img[(y // z) * tw + x // z]
                    rgba += [r, g, b, int(al * 255)]
        mk.png(a.preview, 2 * tw * z, th * z, rgba)


if __name__ == '__main__':
    main()
