#!/usr/bin/env python3
"""Write an LhA archive (level 2 headers, files stored: method -lh0-).
usage: lha.py ARCHIVE.lha ROOT NAME...   (NAME: files or drawers under ROOT)
No compression - small files, and every LhA (Amiga LhA, lhasa, lha for
Unix) reads it. SimpleDock - Copyright (C) 2026 renewable-intel, GPL-3.0-or-later"""
import os, struct, sys


def crc16(data, crc=0):
    """CRC-16/ARC, as LhA uses."""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def header(method, data, mtime, dirparts, name):
    ext = [b'\x00\x00\x00']                      # common header: header CRC (filled in below)
    if name:
        ext.append(b'\x01' + name.encode('latin-1'))
    if dirparts:
        ext.append(b'\x02' + b''.join(p.encode('latin-1') + b'\xff' for p in dirparts))
    # each extended header is followed by the size of the next one (0: last)
    exts = b''
    for i, e in enumerate(ext):
        nxt = len(ext[i + 1]) + 2 if i + 1 < len(ext) else 0
        exts += e + struct.pack('<H', nxt)
    first = len(ext[0]) + 2
    base = method + struct.pack('<IIIBBH', len(data), len(data), int(mtime), 0, 2, crc16(data)) + \
        b'A' + struct.pack('<H', first)
    total = 2 + len(base) + len(exts)
    h = bytearray(struct.pack('<H', total) + base + exts)
    hcrc = crc16(bytes(h))                       # over the whole header, CRC field 0
    pos = 2 + len(base) + 1                      # just after the common header's type byte
    h[pos:pos + 2] = struct.pack('<H', hcrc)
    return bytes(h)


def add(out, root, rel):
    path = os.path.join(root, rel)
    parts = rel.split('/')
    mtime = os.path.getmtime(path)
    if os.path.isdir(path):
        out.write(header(b'-lhd-', b'', mtime, parts, ''))
        for n in sorted(os.listdir(path)):
            if not n.startswith('.'):
                add(out, root, rel + '/' + n)
    else:
        data = open(path, 'rb').read()
        out.write(header(b'-lh0-', data, mtime, parts[:-1], parts[-1]) + data)


def main():
    archive, root, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    with open(archive, 'wb') as out:
        for n in names:
            add(out, root, n)
        out.write(b'\x00')                       # end of archive


if __name__ == '__main__':
    main()
