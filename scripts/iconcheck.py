#!/usr/bin/env python3
"""Read an Amiga .info file: classic DiskObject fields, tooltypes, and the
OS 3.5 colour icon (FORM ICON: FACE + IMAG chunks); writes the decoded
images as PNG for a look.  usage: iconcheck.py ICON.info [OUT_PREFIX]
SimpleDock - Copyright (C) 2026 renewable-intel, GPL-3.0-or-later"""
import struct, sys, zlib


def png(path, w, h, rgba):
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def bits(data, depth):
    """Values of 'depth' bits, then control bytes are read with depth 8."""
    class R:
        def __init__(s): s.pos = 0
        def get(s, n):
            v = 0
            for _ in range(n):
                byte = data[s.pos >> 3] if (s.pos >> 3) < len(data) else 0
                v = v << 1 | (byte >> (7 - (s.pos & 7))) & 1
                s.pos += 1
            return v
    return R()


def unpack(data, fmt, depth, count):
    if fmt == 0:                                  # uncompressed: one value per 'depth' bits
        r = bits(data, depth)
        return [r.get(depth) for _ in range(count)]
    r, out = bits(data, depth), []
    while len(out) < count:
        c = r.get(8)
        if c < 128: out += [r.get(depth) for _ in range(c + 1)]
        elif c > 128: out += [r.get(depth)] * (257 - c)
    return out[:count]


def image_size(d, o):
    w, h, depth = struct.unpack('>hhh', d[o + 4:o + 10])
    pick = d[o + 14]
    planes = bin(pick & ((1 << depth) - 1)).count('1') if depth else 0
    return 20 + ((w + 15) // 16) * 2 * h * planes


def main():
    d = open(sys.argv[1], 'rb').read()
    out = sys.argv[2] if len(sys.argv) > 2 else None
    magic, ver = struct.unpack('>HH', d[:4])
    assert magic == 0xE310, 'not a DiskObject'
    gw, gh, gflags = struct.unpack('>hhH', d[4 + 8:4 + 14])
    rend, srend = struct.unpack('>II', d[4 + 18:4 + 26])
    userdata = struct.unpack('>I', d[4 + 40:4 + 44])[0]
    typ = d[48]
    dtool, ttypes, cx, cy, ddata, twin, stack = struct.unpack('>IIiiIII', d[50:78])
    print(f'magic ok, version {ver}, type {typ}, gadget {gw}x{gh} flags {gflags:#x}, userdata {userdata}, stack {stack}')
    o = 78 + (56 if ddata else 0)
    for name, p in (('normal', rend), ('select', srend)):
        if p:
            w, h, depth = struct.unpack('>hhh', d[o + 4:o + 10])
            print(f'classic {name}: {w}x{h}x{depth}')
            o += image_size(d, o)
    if dtool:
        n = struct.unpack('>I', d[o:o + 4])[0]; print('default tool', d[o + 4:o + 4 + n]); o += 4 + n
    if ttypes:
        cnt = struct.unpack('>I', d[o:o + 4])[0] // 4 - 1; o += 4
        for _ in range(cnt):
            n = struct.unpack('>I', d[o:o + 4])[0]; print('  tooltype', d[o + 4:o + 4 + n].rstrip(b'\0').decode('latin-1')); o += 4 + n
    if twin:
        n = struct.unpack('>I', d[o:o + 4])[0]; o += 4 + n
    if ddata and typ in (1, 2, 5, 6, 8):          # DrawerData2 for drawers / disks / trash
        o += 6
    if d[o:o + 4] != b'FORM':
        print('no colour icon at', o, 'file size', len(d)); return
    end = o + 8 + struct.unpack('>I', d[o + 4:o + 8])[0]
    assert d[o + 8:o + 12] == b'ICON'
    o += 12
    fw = fh = 0; k = 0; pal = None
    while o < end:
        cid, n = d[o:o + 4], struct.unpack('>I', d[o + 4:o + 8])[0]
        b = d[o + 8:o + 8 + n]
        if cid == b'FACE':
            fw, fh = b[0] + 1, b[1] + 1
            print(f'FACE {fw}x{fh} flags {b[2]} aspect {b[3]:#x} maxpal {struct.unpack(">H", b[4:6])[0]}')
        elif cid == b'IMAG':
            tc, nc, fl, ifmt, pfmt, depth = b[:6]
            isz, psz = struct.unpack('>HH', b[6:10])
            idata = b[10:10 + isz + 1]
            pix = unpack(idata, ifmt, depth, fw * fh)
            if fl & 2:
                pv = unpack(b[10 + isz + 1:10 + isz + 1 + psz + 1], pfmt, 8, (nc + 1) * 3)
                pal = [tuple(pv[i * 3:i * 3 + 3]) for i in range(nc + 1)]
            print(f'IMAG {k}: transparent {tc if fl & 1 else None} colours {nc + 1} fmt {ifmt}/{pfmt} depth {depth} '
                  f'bytes {isz + 1}/{psz + 1 if fl & 2 else 0}, max index {max(pix)}')
            if out and pal:
                rgba = []
                for v in pix:
                    rgba += [0, 0, 0, 0] if (fl & 1 and v == tc) else list(pal[v % len(pal)]) + [255]
                png(f'{out}_{k}.png', fw, fh, rgba)
            k += 1
        o += 8 + n + (n & 1)


main()
