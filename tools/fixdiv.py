#!/usr/bin/env python3
"""Work around an LLVM 18 m68k encoder bug: 32-bit DIVU.L/DIVS.L Ds,Dq is
emitted with the remainder field Dr = D0 instead of Dr = Dq, so on real
hardware D0 gets overwritten with the remainder.

usage: fixdiv.py file.mir file.o
Counts every div in llc's final machine IR, finds exactly that many
matching (mis-encoded) instruction words in the object's code sections and
patches the extension word. Any count mismatch aborts the build."""
import re, struct, sys
from collections import Counter

mir, obj = sys.argv[1], sys.argv[2]
want = Counter()
for line in open(mir):
    if 'DIV' not in line: continue
    m = re.search(r'\$d([0-7]) = ([SU])DIVd32d32 (?:killed )?\$d([0-7]), (?:killed )?\$d([0-7])', line)
    if not m:
        sys.exit('fixdiv: unhandled division form in MIR: ' + line.strip())
    q, sg, q2, s_ = int(m.group(1)), m.group(2) == 'S', int(m.group(3)), int(m.group(4))
    if q != q2: sys.exit('fixdiv: unexpected operands: ' + line.strip())
    if q == 0: continue                          # Dr = Dq = 0 is already correct
    want[(s_, q, sg)] += 1
if not want: sys.exit(0)

d = bytearray(open(obj, 'rb').read())
e_shoff, = struct.unpack('>I', d[32:36])
e_shentsize, e_shnum = struct.unpack('>HH', d[46:50])
found = Counter(); spots = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    name, typ, flags, addr, offset, size = struct.unpack('>IIIIII', d[off:off + 24])
    if typ != 1 or not (flags & 4): continue    # PROGBITS + EXECINSTR
    for p in range(offset, offset + size - 3, 2):
        op, ext = struct.unpack('>HH', d[p:p + 4])
        if (op & 0xFFF8) != 0x4C40 or (ext & 0x8FFF) not in (0x0000, 0x0800) or ext & 0x0400: continue
        key = (op & 7, (ext >> 12) & 7, bool(ext & 0x0800))
        if key in want:
            found[key] += 1; spots.append((p, key))
if found != want:
    sys.exit('fixdiv: %s: expected %s, found %s' % (obj, dict(want), dict(found)))
for p, (s, q, sg) in spots:
    struct.pack_into('>H', d, p + 2, (q << 12) | q | (0x0800 if sg else 0))
open(obj, 'wb').write(d)
print('  %s: re-encoded %d divisions' % (obj.split('/')[-1], len(spots)))
