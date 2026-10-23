#!/usr/bin/env python3
"""Make SimpleDock.info: a tool icon with a classic 4-colour image (any
Workbench) and an OS 3.5+ colour image (FORM ICON), plus its tooltypes.  usage: mkicon.py OUT.info [PREVIEW.png]
The program icon is the pixel map in icon.txt; the ReadMe icon is drawn here
(no Amiga or third-party images are used).
SimpleDock - Copyright (C) 2026 renewable-intel, GPL-3.0-or-later"""
import math, os, struct, sys, zlib

W, H, SS = 46, 47, 4                      # size of drawn icons, supersampling
ICON_TXT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.txt")
shade = None                              # the drawing render() uses: (x, y) -> (r, g, b, a)

TOOLTYPES = [                             # settings live in ENVARC:SimpleDock.config
    "DONOTWAIT",
    "(CONFIG=ENVARC:SimpleDock.config)",
]


# ---------------------------------------------------------------- drawing
def rrect_cov(x, y, x0, y0, x1, y1, r):
    """1 if (x, y) is inside the rounded rectangle [x0, x1) x [y0, y1)."""
    if x < x0 or x >= x1 or y < y0 or y >= y1:
        return 0
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return 1 if (x - cx) ** 2 + (y - cy) ** 2 <= r * r else 0


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def render(dark=1.0):
    img = []
    for py in range(H):
        for px in range(W):
            acc, n = [0.0, 0.0, 0.0, 0.0], SS * SS
            for sy in range(SS):
                for sx in range(SS):
                    r, g, b, a = shade(px + (sx + 0.5) / SS, py + (sy + 0.5) / SS)
                    acc[0] += r * a; acc[1] += g * a; acc[2] += b * a; acc[3] += a
            a = acc[3] / n
            if acc[3] > 0:
                c = tuple(min(255, int(acc[i] / acc[3] * dark + 0.5)) for i in range(3))
            else:
                c = (0, 0, 0)
            img.append(c + (a,))
    return img


# ---------------------------------------------------------------- colour icon
def quantise(img, maxc=63):
    """Palette (index 0 = transparent) and pixel indices; edges with alpha
    below a half are transparent, the rest opaque (Workbench has no alpha
    for these icons). Up to maxc colours are kept exactly."""
    exact = sorted({p[:3] for p in img if p[3] >= 0.5})
    if len(exact) <= maxc:
        pal = [(0, 0, 0)] + exact
        index = {c: i for i, c in enumerate(pal)}
        return pal, [index[p[:3]] if p[3] >= 0.5 else 0 for p in img]
    step = 4
    while True:
        cols = {}
        for r, g, b, a in img:
            if a >= 0.5:
                k = (r // step * step, g // step * step, b // step * step)
                cols[k] = cols.get(k, 0) + 1
        if len(cols) <= maxc:
            break
        step *= 2 if step < 32 else 1
        if step >= 32:
            cols = dict(sorted(cols.items(), key=lambda kv: -kv[1])[:maxc]); break
    pal = [(0, 0, 0)] + sorted(cols)
    def nearest(c):
        return min(range(1, len(pal)), key=lambda i: sum((pal[i][j] - c[j]) ** 2 for j in range(3)))
    cache, pix = {}, []
    for r, g, b, a in img:
        if a < 0.5:
            pix.append(0); continue
        k = (r, g, b)
        if k not in cache:
            cache[k] = nearest(k)
        pix.append(cache[k])
    return pal, pix


class Bits:
    def __init__(self): self.v, self.n, self.out = 0, 0, bytearray()
    def put(self, value, width):
        for i in range(width - 1, -1, -1):
            self.v = self.v << 1 | (value >> i) & 1
            self.n += 1
            if self.n == 8:
                self.out.append(self.v); self.v = self.n = 0
    def bytes(self):
        if self.n:
            self.out.append(self.v << (8 - self.n)); self.v = self.n = 0
        return bytes(self.out)


def rle(values, depth):
    """Colour-icon run-length coding: 8-bit control values, data in 'depth'
    bits; 0..127 = n+1 literals follow, 129..255 = next value 257-c times."""
    b, i, n = Bits(), 0, len(values)
    while i < n:
        run = 1
        while i + run < n and run < 128 and values[i + run] == values[i]:
            run += 1
        if run >= 3:
            b.put(257 - run, 8); b.put(values[i], depth); i += run
            continue
        j = i
        while j < n and j - i < 128:
            if j + 2 < n and values[j] == values[j + 1] == values[j + 2]:
                break
            j += 1
        b.put(j - i - 1, 8)
        for v in values[i:j]:
            b.put(v, depth)
        i = j
    return b.bytes()


def chunk(cid, body):
    return cid + struct.pack('>I', len(body)) + body + (b'\0' if len(body) & 1 else b'')


def colour_icon(images):
    imags, maxpal = [], 0
    for pal, pix in images:
        depth = max(1, math.ceil(math.log2(len(pal))))
        data = rle(pix, depth)
        pdata = rle([c for rgb in pal for c in rgb], 8)
        maxpal = max(maxpal, len(pdata))
        imags.append(struct.pack('>BBBBBBHH', 0, len(pal) - 1, 1 | 2, 1, 1, depth, len(data) - 1, len(pdata) - 1)
                     + data + pdata)
    face = struct.pack('>BBBBH', W - 1, H - 1, 1, 0, maxpal - 1)   # flags 1: frameless (the shape, no box)
    body = b'ICON' + chunk(b'FACE', face) + b''.join(chunk(b'IMAG', m) for m in imags)
    return b'FORM' + struct.pack('>I', len(body)) + body


# ---------------------------------------------------------------- classic icon
WBPAL = [(170, 170, 170), (0, 0, 0), (255, 255, 255), (102, 136, 187)]   # Workbench 3.x pens 0-3


def classic_planes(img):
    bpr = (W + 15) // 16 * 2
    planes = [bytearray(bpr * H), bytearray(bpr * H)]
    for y in range(H):
        for x in range(W):
            r, g, b, a = img[y * W + x]
            pen = 0 if a < 0.5 else min(range(4), key=lambda p: sum((WBPAL[p][j] - (r, g, b)[j]) ** 2 for j in range(3)))
            for p in range(2):
                if pen >> p & 1:
                    planes[p][y * bpr + x // 8] |= 0x80 >> (x & 7)
    return bytes(planes[0] + planes[1])


def image_struct(data):
    return struct.pack('>hhhhhIBBI', 0, 0, W, H, 2, 1, 3, 0, 0) + data


WBDRAWER, WBTOOL, WBPROJECT = 2, 3, 4


def diskobject(img_n, img_s, tooltypes, kind=WBTOOL, tool=None):
    """The classic part of an icon: a tool (tooltypes), a drawer (window
    position and size, view by default) or a project (its default tool)."""
    drawer = kind == WBDRAWER
    gadget = struct.pack('>IhhhhHHHIIIIIhI', 0, 0, 0, W, H + 1, 0x0006, 0x0003, 0x0001,
                         1, 1, 0, 0, 0, 0, 1)          # images present, revision 1
    head = struct.pack('>HH', 0xE310, 1) + gadget + struct.pack(
        '>BBIIiiIII', kind, 0, 1 if tool else 0, 1 if tooltypes else 0,
        -0x80000000, -0x80000000, 1 if drawer else 0, 0, 0 if drawer else (32768 if kind == WBTOOL else 4096))
    assert len(head) == 78
    out = head
    if drawer:                                   # NewWindow (48 bytes) + current x, y
        out += struct.pack('>hhhhBBIIIIIIIhhHHH', 60, 40, 420, 160, 255, 255, 0, 0, 0, 0, 0, 0, 0,
                           90, 40, 0xFFFF, 0xFFFF, 1) + struct.pack('>ii', 0, 0)
    out += image_struct(classic_planes(img_n)) + image_struct(classic_planes(img_s))
    if tool:
        out += struct.pack('>I', len(tool) + 1) + tool.encode('latin-1') + b'\0'
    if tooltypes:
        out += struct.pack('>I', (len(tooltypes) + 1) * 4) + b''.join(
            struct.pack('>I', len(t) + 1) + t.encode('latin-1') + b'\0' for t in tooltypes)
    if drawer:                                   # DrawerData (OS 2+): flags, view modes - by default
        out += struct.pack('>IH', 0, 0)
    return out


def shade_page(x, y):
    """The ReadMe icon: a flat white page with a folded corner and text lines."""
    col, a = (0, 0, 0), 0.0

    def over(c, alpha):
        nonlocal col, a
        if alpha <= 0:
            return
        na = alpha + a * (1 - alpha)
        col = tuple((c[i] * alpha + col[i] * a * (1 - alpha)) / na for i in range(3))
        a = na

    x0, y0, x1, y1, f = 9, 2, 36, 44, 8          # page box, fold size

    def page(px, py):                            # inside the page outline (corner cut off)?
        return x0 <= px < x1 and y0 <= py < y1 and px - (x1 - f) <= py - y0
    if page(x - 1, y - 1):
        over((0, 0, 0), 0.35)                    # shadow
    if page(x, y):
        over((34, 40, 52), 1)                    # outline
        if x0 + 1 <= x < x1 - 1 and y0 + 1 <= y < y1 - 1 and x - (x1 - f) <= y - y0 - 1.5:
            over((244, 244, 244), 1)             # paper
        if x1 - f + 1 <= x and y < y0 + f - 1 and x - (x1 - f) <= y - y0 - 1.5:
            over((200, 200, 204), 1)             # the folded corner
    for ly, lx1 in ((14, 31), (19, 31), (24, 27), (29, 31), (34, 24)):
        if x0 + 4 <= x < lx1 and ly <= y < ly + 1.5:
            over((120, 124, 132), 1)             # text lines
    return col + (a,)


def program_icon():
    """The SimpleDock picture from icon.txt: width, height, normal and selected
    images (the selected one darker, as Workbench shows a clicked icon)."""
    pal, rows = {}, []
    for line in open(ICON_TXT, encoding="ascii"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        if " " in line:                          # palette: letter red green blue
            k, r, g, b = line.split()
            pal[k] = (int(r), int(g), int(b))
        else:
            rows.append(line)
    normal = [pal[c] + (1.0,) for row in rows for c in row]
    selected = [tuple(int(v * 0.7) for v in p[:3]) + (1.0,) for p in normal]
    return len(rows[0]), len(rows), normal, selected


def write_icon(path, kind=WBTOOL, tooltypes=None, tool=None, art=None, size=None):
    """An icon file: art None = the SimpleDock picture (icon.txt), else a shade
    function drawn at size (w, h), or 46 x 47."""
    global shade, W, H
    keep = shade, W, H
    if art:
        shade = art
    if size:
        W, H = size
    try:
        if art:
            normal, selected = render(), render(0.7)
        else:
            W, H, normal, selected = program_icon()
        data = diskobject(normal, selected, tooltypes or [], kind, tool) + \
            colour_icon([quantise(normal), quantise(selected)])
    finally:
        shade, W, H = keep
    open(path, 'wb').write(data)
    return normal, selected


def png(path, w, h, rgba):
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def ch(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))


def main():
    global W, H
    normal, selected = write_icon(sys.argv[1], WBTOOL, TOOLTYPES)
    W, H = program_icon()[:2]
    data = open(sys.argv[1], 'rb').read()
    print(f'{sys.argv[1]}: {len(data)} bytes')
    if len(sys.argv) > 2:                        # 4x preview, normal and selected side by side
        z, rgba = 4, []
        for y in range(H * z):
            for img in (normal, selected):
                for x in range(W * z):
                    r, g, b, a = img[(y // z) * W + x // z]
                    rgba += [r, g, b, int(a * 255)]
        png(sys.argv[2], 2 * W * z, H * z, rgba)


if __name__ == '__main__':
    main()
