#!/usr/bin/env python3
"""Work around an LLVM 18 m68k codegen bug: register COPYs are expanded into
MOVE after the condition codes were already allocated, so a COPY placed
between a compare and its branch becomes a MOVE that overwrites the flags
(on real 680x0 hardware MOVE sets N/Z and clears V/C).

Input: machine IR stopped before 'postrapseudos' (COPYs still visible).
For every instruction that reads $ccr, walk back to the instruction that
really defines it; if any COPY into a data register sits in between, wrap
those COPYs in
    move.w %ccr,-(%sp)   (0x42E7)
    move.w (%sp)+,%ccr   (0x44DF)
and shift SP-relative displacements inside the bracket by 2.

usage: fixccr.py in.mir out.mir"""
import re, sys

SAVE = '    INLINEASM &".short 0x42E7", 1 /* sideeffect attdialect */\n'
RESTORE = '    INLINEASM &".short 0x44DF", 1 /* sideeffect attdialect */\n'

uses_ccr = re.compile(r'implicit (?:killed )?\$ccr\b')
defs_ccr = re.compile(r'implicit-def \$ccr\b')
copy_re = re.compile(r'^\s+(?:renamable )?\$(\w+) = COPY ')
DATA = re.compile(r'^(d[0-7]|bd[0-7]|wd[0-7])$')

def is_instr(line):
    s = line.strip()
    return bool(s) and not s.startswith(('bb.', 'successors:', 'liveins:', ';', '#')) and not s.endswith(':')

def process_function(lines, fname, report):
    """lines: the body of one function. Returns new lines and number of fixes."""
    # split into blocks
    blocks = []          # (header_index, [line indices])
    cur = None
    for idx, l in enumerate(lines):
        m = re.match(r'^  bb\.(\d+)', l)
        if m:
            cur = {'num': int(m.group(1)), 'start': idx, 'lines': [], 'succ': [], 'liveccr': False}
            blocks.append(cur); continue
        if cur is None: continue
        st = l.strip()
        if st.startswith('successors:'):
            cur['succ'] = [int(x) for x in re.findall(r'%bb\.(\d+)', st)]
        elif st.startswith('liveins:'):
            cur['liveccr'] = '$ccr' in st
        elif is_instr(l):
            cur['lines'].append(idx)
    bynum = {b['num']: b for b in blocks}
    insert_before = {}   # line index -> text
    insert_after = {}
    fixes = 0
    for b in blocks:
        live = any(bynum[n]['liveccr'] for n in b['succ'] if n in bynum)
        for idx in reversed(b['lines']):
            l = lines[idx]
            m = copy_re.match(l)
            if m and m.group(1) == 'ccr':          # $ccr = COPY $dN  (move to ccr): defines flags
                live = False
                continue
            if m and re.search(r'COPY (?:killed )?\$ccr\b', l):   # $dN = COPY $ccr: reads flags, no clobber
                live = True
                continue
            if m:
                if live and DATA.match(m.group(1)):
                    insert_before[idx] = SAVE
                    insert_after[idx] = RESTORE
                    lines[idx] = re.sub(r'(?<![\w.])(-?\d+), \$sp\b', lambda mm: '%d, $sp' % (int(mm.group(1)) + 2), l)
                    fixes += 1
                continue
            if 'INLINEASM' in l:
                continue
            uses = bool(uses_ccr.search(l))
            defs = bool(re.search(r'implicit-def (?:dead )?\$ccr\b', l))
            if defs and live and 'implicit-def dead $ccr' in l:
                report.append('%s: bb.%d: dead flag def inside a live flag range: %s' % (fname, b['num'], l.strip()))
            if defs: live = False
            if uses: live = True
        if live and not b['liveccr']:
            report.append('%s: bb.%d needs $ccr on entry but does not list it as live-in' % (fname, b['num']))
    out = []
    for idx, l in enumerate(lines):
        if idx in insert_before: out.append(insert_before[idx])
        out.append(l)
        if idx in insert_after: out.append(insert_after[idx])
    return out, fixes

SETD = re.compile(r'^(\s+)(?:renamable )?\$bd([0-7]) = SETd8\w+ ')
SETP = re.compile(r'^(\s+)SETp8\w+ (-?\d+), (?:killed )?\$a([0-7])\b')

def fix_scc(src):
    """Scc stores $FF for true; the backend assumes 1. Normalise with neg.b,
    keeping the flags intact (the Scc result is often followed by more uses of $ccr)."""
    out, n = [], 0
    for l in src:
        out.append(l)
        m = SETD.match(l)
        if m:
            out.append('%sINLINEASM &".short 0x42E7, 0x%04X, 0x44DF", 1 /* sideeffect attdialect */\n' % (m.group(1), 0x4400 | int(m.group(2))))
            n += 1; continue
        m = SETP.match(l)
        if m:
            disp = int(m.group(2)) & 0xFFFF
            out.append('%sINLINEASM &".short 0x42E7, 0x%04X, 0x%04X, 0x44DF", 1 /* sideeffect attdialect */\n' % (m.group(1), 0x4428 | int(m.group(3)), disp))
            n += 1; continue
        if re.search(r'= SET(?!CS_C)\w+|^\s+SET(?!CS_C)\w+', l):
            sys.exit('fixccr: unhandled Scc form: ' + l.strip())
    return out, n

LD16 = re.compile(r'^(\s+)\$(wd[0-7]) = MOVM16mp_P (-?\d+), \$sp\b(.*)$')
ST16 = re.compile(r'^(\s+)MOVM16pm_P (-?\d+), \$sp, (killed )?\$(wd[0-7])\b(.*)$')

def fix_movm16(src):
    """LLVM 18 encodes 16-bit MOVEM spills/reloads as MOVEM.L, clobbering the
    neighbouring stack slot. Use move.w instead, with the flags preserved
    (MOVEM leaves them alone, MOVE does not). SP is 2 lower inside the bracket."""
    out, n = [], 0
    for l in src:
        m = LD16.match(l.rstrip('\n'))
        if m:
            ind, r, d, rest = m.groups()
            out += [SAVE, '%s$%s = MOV16rp %d, $sp, implicit-def $ccr%s\n' % (ind, r, int(d) + 2, rest), RESTORE]
            n += 1; continue
        m = ST16.match(l.rstrip('\n'))
        if m:
            ind, d, k, r, rest = m.groups()
            out += [SAVE, '%sMOV16pr %d, $sp, %s$%s, implicit-def $ccr%s\n' % (ind, int(d) + 2, k or '', r, rest), RESTORE]
            n += 1; continue
        if 'MOVM16' in l:
            sys.exit('fixccr: unhandled 16-bit MOVEM: ' + l.strip())
        out.append(l)
    return out, n

def fix_btst(src):
    """LLVM 18 assumes Z is set when the tested bit is 1; on the 680x0, BTST
    sets Z when the bit is 0. Swap eq/ne on every consumer of a BTST's flags."""
    n = 0
    i = 0
    while i < len(src):
        if re.search(r'\bBTST\w+ ', src[i]):
            j, consumers = i + 1, 0
            while j < len(src) and not re.match(r'^  bb\.\d+', src[j]):
                l = src[j]
                if uses_ccr.search(l):
                    m = re.search(r'\b(B|SETd8)(eq|ne)(8|16|32)?\b', l)
                    if not m: sys.exit('fixccr: BTST flags consumed by unexpected instruction: ' + l.strip())
                    src[j] = l[:m.start(2)] + ('ne' if m.group(2) == 'eq' else 'eq') + l[m.end(2):]
                    consumers += 1
                if re.search(r'implicit-def (?:dead )?\$ccr\b', l): break
                j += 1
            else:
                # reached the end of the block: flags must not flow into successors
                k = j + 1
                while k < len(src) and k < j + 4:
                    if 'liveins:' in src[k] and '$ccr' in src[k]:
                        sys.exit('fixccr: BTST flags live across blocks (not handled)')
                    k += 1
            if consumers == 0: sys.exit('fixccr: BTST with no flag consumer: ' + src[i].strip())
            n += 1
        i += 1
    return src, n

CCR_SAVE = re.compile(r'^(\s+)\$d([0-7]) = COPY (?:killed )?\$ccr\s*$')
CCR_LOAD = re.compile(r'^(\s+)\$ccr = COPY (?:killed )?\$d([0-7])\s*$')

def fix_ccr_copies(src):
    """LLVM 18 expands COPY to/from $ccr into a broken MOVE (source register
    lost, and a privileged MOVE to SR). Emit correct raw encodings instead:
    move.w %ccr,%dN = 0x42C0|N, move.w %dN,%ccr = 0x44C0|N."""
    out, n = [], 0
    for l in src:
        m = CCR_SAVE.match(l.rstrip('\n'))
        if m:
            # movem doesn't touch the flags; it only gives $dN a definition for the verifier
            out.append('%s$d%s = MOVM32mp_P 0, $sp\n' % (m.group(1), m.group(2)))
            out.append('%sINLINEASM &".short 0x%04X", 1 /* sideeffect attdialect */\n' % (m.group(1), 0x42C0 | int(m.group(2)))); n += 1; continue
        m = CCR_LOAD.match(l.rstrip('\n'))
        if m:
            # cmp defines $ccr for the verifier; the raw move then sets the real flags
            out.append('%sCMP32di 0, $d%s, implicit-def $ccr\n' % (m.group(1), m.group(2)))
            out.append('%sINLINEASM &".short 0x%04X", 1 /* sideeffect attdialect */\n' % (m.group(1), 0x44C0 | int(m.group(2)))); n += 1; continue
        if re.search(r'COPY .*\$ccr|\$ccr = COPY', l):
            sys.exit('fixccr: unhandled $ccr copy: ' + l.strip())
        out.append(l)
    return out, n

SETCSC = re.compile(r'^(\s+)\$(d|bd)([0-7]) = SETCS_C(32|8)d .*$')

def fix_setcs(src):
    """LLVM 18 lowers SETCS_C (0 or -1 from the carry) to SUBX Dn,Dn, which
    reads X. A preceding CMP sets C but never X, so the result depends on a
    stale X. Use SCS (+EXTB.L for 32-bit), which read C directly."""
    out, n = [], 0
    for l in src:
        m = SETCSC.match(l.rstrip('\n'))
        if m:
            ind, kind, r, bits = m.group(1), m.group(2), int(m.group(3)), m.group(4)
            words = '0x%04X' % (0x55C0 | r) + (', 0x%04X' % (0x49C0 | r) if bits == '32' else '')
            out.append('%s$d%d = MOVM32mp_P 0, $sp\n' % (ind, r))   # flag-neutral def for the verifier
            out.append('%sINLINEASM &".short %s", 1 /* sideeffect attdialect */\n' % (ind, words))
            n += 1; continue
        if 'SETCS_C' in l or re.search(r'\b(ADDX|SUBX)\w*', l):
            sys.exit('fixccr: unhandled X-flag instruction: ' + l.strip())
        out.append(l)
    return out, n

ZXA = re.compile(r'^(\s+)\$a([0-7]) = MOVZXd32d16 (?:killed )?\$wd([0-7]), implicit-def dead \$ccr(?:, implicit killed \$d[0-7])?\s*$')

def fix_zext_areg(src):
    """LLVM 18 expands MOVZXd32d16 into an address register as MOVE.L + AND.L,
    and the AND lands on the *data* register with the same number. Zero-extend
    through the stack instead: move.w dM,-(sp); clr.w -(sp); movea.l (sp)+,aN."""
    out, n = [], 0
    for l in src:
        m = ZXA.match(l.rstrip('\n'))
        if m:
            ind, an, dm = m.group(1), int(m.group(2)), int(m.group(3))
            out.append('%s$a%d = LEA32p 0, $sp\n' % (ind, an))   # flag-neutral def for the verifier
            out.append('%sINLINEASM &".short 0x%04X, 0x4267, 0x%04X", 1 /* sideeffect attdialect */\n' % (ind, 0x3F00 | dm, 0x205F | (an << 9)))
            n += 1; continue
        if re.search(r'\$a[0-7] = MOV[SZ]X', l):
            sys.exit('fixccr: unhandled extend into address register: ' + l.strip())
        out.append(l)
    return out, n

def main():
    src = [l + '\n' for l in open(sys.argv[1]).read().split('\n')]
    src, nscc = fix_scc(src)
    src, nbt = fix_btst(src)
    if nbt: print('  %s: inverted %d BTST conditions' % (sys.argv[1].split('/')[-1], nbt))
    out, report, total = [], [], 0
    fname = '?'
    i = 0
    while i < len(src):
        l = src[i]
        m = re.match(r'^name:\s+(\S+)', l)
        if m: fname = m.group(1)
        if l.startswith('body:'):
            j = i + 1
            while j < len(src) and not src[j].startswith(('...', '---')):
                j += 1
            body, n = process_function(src[i + 1:j], fname, report)
            total += n
            out.append(l); out.extend(body)
            i = j
            continue
        out.append(l)
        i += 1
    out, nc = fix_ccr_copies(out)
    if nc: print('  %s: re-encoded %d flag-register copies' % (sys.argv[1].split('/')[-1], nc))
    out, ncs = fix_setcs(out)
    if ncs: print('  %s: replaced %d carry-mask (SUBX) sequences' % (sys.argv[1].split('/')[-1], ncs))
    out, nz = fix_zext_areg(out)
    if nz: print('  %s: fixed %d zero-extends into address registers' % (sys.argv[1].split('/')[-1], nz))
    out, n16 = fix_movm16(out)
    if n16: print('  %s: rewrote %d 16-bit MOVEM spills' % (sys.argv[1].split('/')[-1], n16))
    text = ''.join(out)
    if text.endswith('\n\n'): text = text[:-1]
    open(sys.argv[2], 'w').write(text)
    for r in report: print('fixccr WARNING:', r)
    if total or nscc: print('  %s: protected flags around %d copies, normalised %d Scc' % (sys.argv[1].split('/')[-1], total, nscc))
    if report: sys.exit(1)

main()
