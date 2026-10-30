#!/usr/bin/env python3
"""Minimal linker: m68k ELF relocatable objects (clang) -> AmigaOS hunk executable.

Everything is placed in ONE code hunk, object by object (each object's text,
rodata, data and bss kept together) because clang's m68k backend addresses
data PC-relative with 16-bit displacements. Absolute 32-bit relocations become
HUNK_RELOC32 entries. Errors on undefined symbols, out-of-range PC16, and any
instruction that stores through a PC-relative address (illegal on 68k).
"""
import struct, sys

R_68K_NONE, R_68K_32, R_68K_16, R_68K_8, R_68K_PC32, R_68K_PC16, R_68K_PC8 = range(7)
R_68K_PLT32 = 13

def writes_pc_ea(image, P):
    """Heuristic: does an instruction ending its EA at P write to (d16,PC)/(d8,PC,Xn)?"""
    for back in (2, 4, 6):
        if P - back < 0: continue
        w = struct.unpack_from('>H', image, P - back)[0]
        top = w >> 12
        if top in (1, 2, 3) and ((w >> 6) & 0x3F) in (0x17, 0x1F):
            return 'move to PC-relative'
        if back != 2: continue
        low6 = w & 0x3F
        if low6 not in (0x3A, 0x3B): continue
        size = (w >> 6) & 3
        if (w & 0xF900) == 0x4000 and ((w >> 9) & 7) in (1, 2, 3) and size != 3:
            return 'clr/neg/not on PC-relative'
        if top == 5 and size != 3:
            return 'addq/subq on PC-relative'
        if top in (0x8, 0x9, 0xB, 0xC, 0xD) and (w & 0x100) and size != 3:
            return 'op Dn,<PC-relative>'
    return None


class Obj:
    pass

def parse(path):
    d = open(path, 'rb').read()
    if d[:4] != b'\x7fELF' or d[4] != 1 or d[5] != 2:
        sys.exit('%s: not a 32-bit big-endian ELF' % path)
    (e_type, e_machine, _, _, _, e_shoff, _, _, _, _, e_shentsize, e_shnum, e_shstrndx) = struct.unpack('>HHIIIIIHHHHHH', d[16:52])
    if e_machine != 4: sys.exit('%s: not m68k' % path)
    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, typ, flags, addr, offset, size, link, info, align, entsize = struct.unpack('>IIIIIIIIII', d[off:off+40])
        secs.append(dict(name=name, type=typ, flags=flags, offset=offset, size=size, link=link, info=info, align=max(align, 1), entsize=entsize))
    shstr = secs[e_shstrndx]
    def cstr(tab, o):
        s = tab['offset'] + o
        return d[s:d.index(b'\0', s)].decode()
    for s in secs: s['nm'] = cstr(shstr, s['name'])
    o = Obj(); o.path = path; o.data = d; o.secs = secs
    o.syms = []
    for s in secs:
        if s['type'] == 2:  # SYMTAB
            strtab = secs[s['link']]
            for k in range(s['size'] // 16):
                so = s['offset'] + k * 16
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack('>IIIBBH', d[so:so+16])
                o.syms.append(dict(name=cstr(strtab, st_name), value=st_value, bind=st_info >> 4, type=st_info & 15, shndx=st_shndx))
    return o

def is_alloc(s):
    return (s['flags'] & 2) and s['type'] in (1, 8) and s['size'] >= 0  # PROGBITS / NOBITS

def link(objs, out, mapfile=None):
    image = bytearray()
    secbase = {}   # (obj idx, sec idx) -> image offset
    order_kw = ['.text', '.rodata', '.data', '.bss']
    for oi, o in enumerate(objs):
        # keep each object's sections together, in a stable order
        idxs = [i for i, s in enumerate(o.secs) if is_alloc(s)]
        def key(i):
            n = o.secs[i]['nm']
            for k, kw in enumerate(order_kw):
                if n.startswith(kw): return (k, i)
            return (len(order_kw), i)
        for i in sorted(idxs, key=key):
            s = o.secs[i]
            al = max(4, s['align'])
            while len(image) % al: image.append(0)
            secbase[(oi, i)] = len(image)
            if s['type'] == 8: image += bytes(s['size'])
            else: image += o.data[s['offset']:s['offset'] + s['size']]
        if len(image) % 4: image += bytes(4 - len(image) % 4)
    # global symbols
    gsym = {}
    for oi, o in enumerate(objs):
        for sym in o.syms:
            if sym['bind'] in (1, 2) and sym['shndx'] not in (0, 0xfff1, 0xfff2) and sym['name']:
                if (oi, sym['shndx']) not in secbase: continue
                addr = secbase[(oi, sym['shndx'])] + sym['value']
                if sym['name'] in gsym and sym['bind'] == 1:
                    sys.exit('duplicate symbol %s' % sym['name'])
                gsym[sym['name']] = addr
    reloc32 = []
    errors = []
    for oi, o in enumerate(objs):
        for s in o.secs:
            if s['type'] != 4: continue  # RELA
            target = s['info']
            if (oi, target) not in secbase: continue
            base = secbase[(oi, target)]
            for k in range(s['size'] // 12):
                ro = s['offset'] + k * 12
                r_offset, r_info, r_addend = struct.unpack('>IIi', o.data[ro:ro+12])
                symi, typ = r_info >> 8, r_info & 0xff
                sym = o.syms[symi]
                if sym['shndx'] == 0:
                    if sym['name'] not in gsym:
                        errors.append('undefined symbol %s (from %s)' % (sym['name'], o.path)); continue
                    S = gsym[sym['name']]
                elif sym['shndx'] == 0xfff1:
                    S = sym['value']
                else:
                    if (oi, sym['shndx']) not in secbase:
                        errors.append('reloc against non-alloc section in %s' % o.path); continue
                    S = secbase[(oi, sym['shndx'])] + sym['value']
                P = base + r_offset
                if typ == R_68K_32:
                    struct.pack_into('>I', image, P, (S + r_addend) & 0xffffffff)
                    reloc32.append(P)
                elif typ == R_68K_PC32:
                    struct.pack_into('>i', image, P, S + r_addend - P)
                elif typ == R_68K_PLT32:
                    prev = struct.unpack_from('>H', image, P - 2)[0]
                    if prev in (0x4EB9, 0x4EF9):      # jsr/jmp abs.l
                        struct.pack_into('>I', image, P, (S + r_addend) & 0xffffffff)
                        reloc32.append(P)
                    else:
                        struct.pack_into('>i', image, P, S + r_addend - P)
                elif typ == R_68K_PC16:
                    if o.secs[target]['nm'].startswith('.text'):
                        bad = writes_pc_ea(image, P)
                        if bad: errors.append('%s at %s+0x%x (%s): a global is being written' % (bad, o.path, r_offset, sym['name'] or '<section>'))
                    v = S + r_addend - P
                    if not -32768 <= v <= 32767:
                        errors.append('PC16 out of range (%d) to %s in %s' % (v, sym['name'] or '<section>', o.path))
                        continue
                    struct.pack_into('>h', image, P, v)
                elif typ == R_68K_PC8:
                    v = S + r_addend - P
                    if not -128 <= v <= 127: errors.append('PC8 out of range in %s' % o.path); continue
                    struct.pack_into('>b', image, P, v)
                elif typ == R_68K_NONE:
                    pass
                else:
                    errors.append('unsupported reloc type %d in %s' % (typ, o.path))
    if errors:
        for e in sorted(set(errors)): print('link error:', e)
        sys.exit(1)
    if '_start' not in gsym or gsym['_start'] != 0:
        sys.exit('_start must be the first thing in the image (put start.o first)')
    n = len(image) // 4
    h = struct.pack('>IIIIII', 0x3F3, 0, 1, 0, 0, n)
    h += struct.pack('>II', 0x3E9, n) + bytes(image)
    if reloc32:
        reloc32.sort()
        h += struct.pack('>III', 0x3EC, len(reloc32), 0)
        h += b''.join(struct.pack('>I', r) for r in reloc32)
        h += struct.pack('>I', 0)
    h += struct.pack('>I', 0x3F2)
    open(out, 'wb').write(h)
    if mapfile:
        with open(mapfile, 'w') as f:
            for name, addr in sorted(gsym.items(), key=lambda x: x[1]):
                f.write('%08x %s\n' % (addr, name))
    print('linked %s: %d bytes code, %d relocs' % (out, len(image), len(reloc32)))

if __name__ == '__main__':
    args = sys.argv[1:]
    out = args[args.index('-o') + 1]; args = [a for i, a in enumerate(args) if a != '-o' and (i == 0 or args[i-1] != '-o')]
    mp = None
    if '-Map' in args:
        mp = args[args.index('-Map') + 1]; i = args.index('-Map'); args = args[:i] + args[i+2:]
    link([parse(p) for p in args], out, mp)
