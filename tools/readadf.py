#!/usr/bin/env python3
"""Independent checker for FFS ADF images: verifies boot/block checksums and
bitmap, walks directories and extracts files. usage: readadf.py img.adf [outdir]"""
import struct, sys, os
BS, HT = 512, 72
img = open(sys.argv[1], 'rb').read()
blk = lambda n: img[n * BS:(n + 1) * BS]
L = lambda b, o: struct.unpack('>I', b[o:o + 4])[0]
def bootsum():
    s = 0
    for i in range(0, 1024, 4):
        s += L(img, i)
        if s > 0xFFFFFFFF: s = (s & 0xFFFFFFFF) + 1
    return s
assert img[:4] == b'DOS\x01', 'not FFS'
assert bootsum() == 0xFFFFFFFF, 'bad boot checksum'
def chk(n, off=20):
    b = blk(n); s = sum(L(b, i) for i in range(0, BS, 4)) & 0xFFFFFFFF
    assert s == 0, 'bad checksum in block %d' % n
root = L(img, 8); chk(root)
rb = blk(root)
assert L(rb, 0) == 2 and struct.unpack('>i', rb[508:512])[0] == 1
bm = L(rb, 316); chk(bm, 0)
used = {0, 1, root, bm}
files = {}
def walk(d, path):
    db = blk(d)
    for k in range(HT):
        n = L(db, 24 + k * 4)
        while n:
            chk(n); used.add(n)
            h = blk(n); name = h[433:433 + h[432]].decode('latin-1')
            st = struct.unpack('>i', h[508:512])[0]
            assert L(h, 500) == d, 'bad parent link for ' + name
            if st == 2: walk(n, path + name + '/')
            else:
                size = L(h, 324); data = bytearray(); hb = h; owner = n
                while True:
                    cnt = L(hb, 8)
                    for j in range(cnt):
                        db_ = L(hb, 24 + (HT - 1 - j) * 4); used.add(db_)
                        data += blk(db_)
                    ext = L(hb, 504)
                    if not ext: break
                    chk(ext); used.add(ext); hb = blk(ext)
                    assert L(hb, 0) == 16 and L(hb, 500) == owner
                files[path + name] = bytes(data[:size])
            n = L(h, 496)
walk(root, '')
b = blk(bm)
for i in range(1758):
    free = (L(b, 4 + (i // 32) * 4) >> (i % 32)) & 1
    assert free == ((i + 2) not in used), 'bitmap mismatch at block %d' % (i + 2)
for f, d in sorted(files.items()): print('  %-24s %7d bytes' % (f, len(d)))
print('OK: checksums, hash chains, extension blocks and bitmap all consistent (%d blocks used)' % len(used))
if len(sys.argv) > 2:
    for f, d in files.items():
        p = os.path.join(sys.argv[2], f); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, 'wb').write(d)
