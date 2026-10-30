/* Small 68020 user-mode interpreter, enough for clang-generated code.
   Aborts loudly on anything unknown so miscompiles and bad pointers surface. */
#include <stdio.h>
#include "sim.h"

Cpu cpu;

/* ---------------- flags ---------------- */
static u32 msk(int sz) { return sz == 1 ? 0xFF : sz == 2 ? 0xFFFF : 0xFFFFFFFF; }
static u32 sgn(int sz) { return sz == 1 ? 0x80 : sz == 2 ? 0x8000 : 0x80000000; }
static s32 sext(u32 v, int sz) { return sz == 1 ? (s32)(s8)v : sz == 2 ? (s32)(s16)v : (s32)v; }
static void setnz(u32 r, int sz) { r &= msk(sz); cpu.n = (r & sgn(sz)) != 0; cpu.z = r == 0; }
static void logic(u32 r, int sz) { setnz(r, sz); cpu.v = 0; cpu.c = 0; }

static u32 do_add(u32 d, u32 s, int sz, int x)
{
    u32 m = msk(sz), r = (d + s + (u32)x) & m;
    u64 full = (u64)(d & m) + (u64)(s & m) + (u64)x;
    cpu.c = cpu.x = (full >> (sz * 8)) & 1;
    cpu.v = (((d ^ r) & (s ^ r)) & sgn(sz)) != 0;
    cpu.n = (r & sgn(sz)) != 0;
    cpu.z = r == 0;
    return r;
}
static u32 do_sub(u32 d, u32 s, int sz, int x, int setx)
{
    u32 m = msk(sz), r = (d - s - (u32)x) & m;
    int c = ((u64)(s & m) + (u64)x) > (u64)(d & m);
    cpu.c = c; if (setx) cpu.x = c;
    cpu.v = (((d ^ s) & (d ^ r)) & sgn(sz)) != 0;
    cpu.n = (r & sgn(sz)) != 0;
    cpu.z = r == 0;
    return r;
}

static int cond(int cc)
{
    switch (cc) {
    case 0: return 1; case 1: return 0;
    case 2: return !cpu.c && !cpu.z; case 3: return cpu.c || cpu.z;
    case 4: return !cpu.c; case 5: return cpu.c;
    case 6: return !cpu.z; case 7: return cpu.z;
    case 8: return !cpu.v; case 9: return cpu.v;
    case 10: return !cpu.n; case 11: return cpu.n;
    case 12: return cpu.n == cpu.v; case 13: return cpu.n != cpu.v;
    case 14: return !cpu.z && cpu.n == cpu.v; default: return cpu.z || cpu.n != cpu.v;
    }
}

/* ---------------- fetch ---------------- */
static u16 fetch16(void) { u16 v = rd16(cpu.pc); cpu.pc += 2; return v; }
static u32 fetch32(void) { u32 v = rd32(cpu.pc); cpu.pc += 4; return v; }

/* ---------------- effective addresses ---------------- */
typedef struct { int kind; u32 addr; int reg; u32 imm; } EA;  /* kind 0 Dn,1 An,2 mem,3 imm */

static u32 index_ext(u32 base, u32 pcbase)
{
    u16 ext = fetch16();
    int reg = (ext >> 12) & 7, isA = (ext >> 15) & 1, longIdx = (ext >> 11) & 1, scale = (ext >> 9) & 3;
    s32 idx = isA ? (s32)cpu.a[reg] : (s32)cpu.d[reg];
    (void)pcbase;
    if (!longIdx) idx = (s16)idx;
    idx <<= scale;
    if (!(ext & 0x100)) return base + (u32)(s32)(s8)(ext & 0xFF) + (u32)idx;
    {   /* full format */
        int bs = (ext >> 7) & 1, is = (ext >> 6) & 1, bdsz = (ext >> 4) & 3, iis = ext & 7;
        s32 bd = 0;
        if (bdsz == 2) bd = (s16)fetch16(); else if (bdsz == 3) bd = (s32)fetch32();
        if (iis) die("memory-indirect addressing not supported");
        return (bs ? 0 : base) + (u32)bd + (is ? 0 : (u32)idx);
    }
}

static EA get_ea(int mode, int reg, int sz)
{
    EA e; e.kind = 2; e.reg = reg; e.addr = 0; e.imm = 0;
    switch (mode) {
    case 0: e.kind = 0; break;
    case 1: e.kind = 1; break;
    case 2: e.addr = cpu.a[reg]; break;
    case 3: e.addr = cpu.a[reg]; cpu.a[reg] += (reg == 7 && sz == 1) ? 2 : (u32)sz; break;
    case 4: cpu.a[reg] -= (reg == 7 && sz == 1) ? 2 : (u32)sz; e.addr = cpu.a[reg]; break;
    case 5: e.addr = cpu.a[reg] + (u32)(s32)(s16)fetch16(); break;
    case 6: e.addr = index_ext(cpu.a[reg], 0); break;
    case 7:
        switch (reg) {
        case 0: e.addr = (u32)(s32)(s16)fetch16(); break;
        case 1: e.addr = fetch32(); break;
        case 2: { u32 pc = cpu.pc; e.addr = pc + (u32)(s32)(s16)fetch16(); break; }
        case 3: { u32 pc = cpu.pc; e.addr = index_ext(pc, pc); break; }
        case 4: e.kind = 3;
            if (sz == 4) e.imm = fetch32(); else { e.imm = fetch16(); if (sz == 1) e.imm &= 0xFF; }
            break;
        default: die("bad EA mode 7/%d", reg);
        }
        break;
    }
    return e;
}

static u32 ea_rd(EA *e, int sz)
{
    switch (e->kind) {
    case 0: return cpu.d[e->reg] & msk(sz);
    case 1: return cpu.a[e->reg] & msk(sz);
    case 3: return e->imm;
    }
    return sz == 1 ? rd8(e->addr) : sz == 2 ? rd16(e->addr) : rd32(e->addr);
}

static void ea_wr(EA *e, int sz, u32 v)
{
    switch (e->kind) {
    case 0: cpu.d[e->reg] = (cpu.d[e->reg] & ~msk(sz)) | (v & msk(sz)); return;
    case 1: cpu.a[e->reg] = v; return;
    case 3: die("write to immediate");
    }
    if (sz == 1) wr8(e->addr, v); else if (sz == 2) wr16(e->addr, v); else wr32(e->addr, v);
}

static int sz_from2(int s) { return s == 0 ? 1 : s == 1 ? 2 : s == 2 ? 4 : 0; }

static void push32(u32 v) { cpu.a[7] -= 4; wr32(cpu.a[7], v); }
static u32 pop32(void) { u32 v = rd32(cpu.a[7]); cpu.a[7] += 4; return v; }

/* ---------------- groups ---------------- */
static void op_imm(u16 op)
{
    int mode = (op >> 3) & 7, reg = op & 7;
    if (op & 0x100) {   /* dynamic bit op: btst/bchg/bclr/bset Dn,<ea> */
        int type = (op >> 6) & 3;
        u32 bit = cpu.d[(op >> 9) & 7];
        EA e = get_ea(mode, reg, mode == 0 ? 4 : 1);
        int sz = mode == 0 ? 4 : 1;
        u32 v = ea_rd(&e, sz);
        bit &= sz == 4 ? 31 : 7;
        cpu.z = !((v >> bit) & 1);
        if (type == 1) ea_wr(&e, sz, v ^ (1u << bit));
        else if (type == 2) ea_wr(&e, sz, v & ~(1u << bit));
        else if (type == 3) ea_wr(&e, sz, v | (1u << bit));
        return;
    }
    if (((op >> 8) & 0xF) == 8) {  /* static bit op */
        int type = (op >> 6) & 3;
        u32 bit = fetch16() & 0xFF;
        int sz = mode == 0 ? 4 : 1;
        EA e = get_ea(mode, reg, sz);
        u32 v = ea_rd(&e, sz);
        bit &= sz == 4 ? 31 : 7;
        cpu.z = !((v >> bit) & 1);
        if (type == 1) ea_wr(&e, sz, v ^ (1u << bit));
        else if (type == 2) ea_wr(&e, sz, v & ~(1u << bit));
        else if (type == 3) ea_wr(&e, sz, v | (1u << bit));
        return;
    }
    {
        int sz = sz_from2((op >> 6) & 3), kind = (op >> 9) & 7;
        u32 imm, v, r;
        EA e;
        if (!sz) die("bad immediate size op %04x", op);
        imm = sz == 4 ? fetch32() : (fetch16() & msk(sz));
        if (mode == 7 && reg == 4) die("op to CCR/SR %04x", op);
        e = get_ea(mode, reg, sz);
        v = ea_rd(&e, sz);
        switch (kind) {
        case 0: r = v | imm; logic(r, sz); ea_wr(&e, sz, r); break;
        case 1: r = v & imm; logic(r, sz); ea_wr(&e, sz, r); break;
        case 2: r = do_sub(v, imm, sz, 0, 1); ea_wr(&e, sz, r); break;
        case 3: r = do_add(v, imm, sz, 0); ea_wr(&e, sz, r); break;
        case 5: r = v ^ imm; logic(r, sz); ea_wr(&e, sz, r); break;
        case 6: do_sub(v, imm, sz, 0, 0); break;
        default: die("unknown immediate op %04x", op);
        }
    }
}

static void op_move(u16 op)
{
    int t = op >> 12, sz = t == 1 ? 1 : t == 3 ? 2 : 4;
    EA s = get_ea((op >> 3) & 7, op & 7, sz);
    u32 v = ea_rd(&s, sz);
    int dmode = (op >> 6) & 7, dreg = (op >> 9) & 7;
    if (dmode == 1) { cpu.a[dreg] = (u32)sext(v, sz); return; }   /* movea */
    if (dmode == 7 && (dreg == 2 || dreg == 3)) die("MOVE to PC-relative destination (illegal instruction)");
    {
        EA d = get_ea(dmode, dreg, sz);
        ea_wr(&d, sz, v);
        logic(v, sz);
    }
}

static void op_movem(u16 op)
{
    int toReg = (op >> 10) & 1, sz = (op & 0x40) ? 4 : 2;
    int mode = (op >> 3) & 7, reg = op & 7, i;
    u16 mask = fetch16();
    if (mode == 4) {   /* predecrement, store; mask reversed */
        u32 addr = cpu.a[reg];
        for (i = 15; i >= 0; i--) {
            if (!(mask & (1 << (15 - i)))) continue;
            addr -= (u32)sz;
            { u32 v = i < 8 ? cpu.d[i] : cpu.a[i - 8]; if (sz == 4) wr32(addr, v); else wr16(addr, v); }
        }
        cpu.a[reg] = addr;
        return;
    }
    {
        u32 addr;
        if (mode == 3) addr = cpu.a[reg];
        else { EA e = get_ea(mode, reg, sz); addr = e.addr; }
        for (i = 0; i < 16; i++) {
            if (!(mask & (1 << i))) continue;
            if (toReg) {
                u32 v = sz == 4 ? rd32(addr) : (u32)(s32)(s16)rd16(addr);
                if (i < 8) cpu.d[i] = v; else cpu.a[i - 8] = v;
            } else {
                u32 v = i < 8 ? cpu.d[i] : cpu.a[i - 8];
                if (sz == 4) wr32(addr, v); else wr16(addr, v);
            }
            addr += (u32)sz;
        }
        if (mode == 3) cpu.a[reg] = addr;
    }
}

static void op_muldivl(u16 op)
{
    u16 ext = fetch16();
    EA e = get_ea((op >> 3) & 7, op & 7, 4);
    u32 src = ea_rd(&e, 4);
    int dl = (ext >> 12) & 7, dh = ext & 7, sgnd = (ext >> 11) & 1, big = (ext >> 10) & 1;
    if (big) die("64-bit MUL/DIV form (traps on 68060)");
    if (!(op & 0x40)) {   /* mul */
        if (sgnd) {
            s64 r = (s64)(s32)cpu.d[dl] * (s64)(s32)src;
            cpu.d[dl] = (u32)r; cpu.v = r != (s64)(s32)r;
        } else {
            u64 r = (u64)cpu.d[dl] * (u64)src;
            cpu.d[dl] = (u32)r; cpu.v = r != (u64)(u32)r;
        }
        cpu.c = 0; setnz(cpu.d[dl], 4);
    } else {              /* div: Dq = dl, Dr = dh */
        if (src == 0) die("division by zero");
        if (sgnd) {
            s32 a = (s32)cpu.d[dl], b = (s32)src;
            s32 q = (a == (s32)0x80000000 && b == -1) ? a : a / b, r = (a == (s32)0x80000000 && b == -1) ? 0 : a % b;
            if (dh != dl) cpu.d[dh] = (u32)r;
            cpu.d[dl] = (u32)q;
        } else {
            u32 a = cpu.d[dl];
            if (dh != dl) cpu.d[dh] = a % src;
            cpu.d[dl] = a / src;
        }
        cpu.v = 0; cpu.c = 0; setnz(cpu.d[dl], 4);
    }
}

static void op_misc(u16 op)
{
    int mode = (op >> 3) & 7, reg = op & 7;
    if ((op & 0xF1C0) == 0x41C0) { EA e = get_ea(mode, reg, 4); cpu.a[(op >> 9) & 7] = e.addr; return; } /* lea */
    if (op == 0x4E75) { cpu.pc = pop32(); return; }                         /* rts */
    if (op == 0x4E71) return;                                              /* nop */
    if ((op & 0xFFC0) == 0x4E80) { EA e = get_ea(mode, reg, 4); push32(cpu.pc); cpu.pc = e.addr; return; } /* jsr */
    if ((op & 0xFFC0) == 0x4EC0) { EA e = get_ea(mode, reg, 4); cpu.pc = e.addr; return; }                /* jmp */
    if ((op & 0xFFF8) == 0x4E50) { s16 d = (s16)fetch16(); push32(cpu.a[reg]); cpu.a[reg] = cpu.a[7]; cpu.a[7] += (u32)(s32)d; return; } /* link */
    if ((op & 0xFFF8) == 0x4E58) { cpu.a[7] = cpu.a[reg]; cpu.a[reg] = pop32(); return; }               /* unlk */
    if ((op & 0xFFF8) == 0x4840) { u32 v = cpu.d[reg]; v = (v >> 16) | (v << 16); cpu.d[reg] = v; logic(v, 4); return; } /* swap */
    if ((op & 0xFFC0) == 0x4840) { EA e = get_ea(mode, reg, 4); push32(e.addr); return; }               /* pea */
    if ((op & 0xFFF8) == 0x4880) { u32 v = (u32)(s32)(s8)cpu.d[reg]; cpu.d[reg] = (cpu.d[reg] & 0xFFFF0000) | (v & 0xFFFF); setnz(v, 2); cpu.v = cpu.c = 0; return; } /* ext.w */
    if ((op & 0xFFF8) == 0x48C0) { cpu.d[reg] = (u32)(s32)(s16)cpu.d[reg]; logic(cpu.d[reg], 4); return; } /* ext.l */
    if ((op & 0xFFF8) == 0x49C0) { cpu.d[reg] = (u32)(s32)(s8)cpu.d[reg]; logic(cpu.d[reg], 4); return; }  /* extb.l */
    if ((op & 0xFFC0) == 0x42C0) { EA e = get_ea(mode, reg, 2); ea_wr(&e, 2, (u32)((cpu.x << 4) | (cpu.n << 3) | (cpu.z << 2) | (cpu.v << 1) | cpu.c)); return; } /* move ccr,<ea> */
    if ((op & 0xFFC0) == 0x44C0) { EA e = get_ea(mode, reg, 2); u32 v = ea_rd(&e, 2); cpu.x = (v >> 4) & 1; cpu.n = (v >> 3) & 1; cpu.z = (v >> 2) & 1; cpu.v = (v >> 1) & 1; cpu.c = v & 1; return; } /* move <ea>,ccr */
    if ((op & 0xFB80) == 0x4880) { op_movem(op); return; }
    if ((op & 0xFF80) == 0x4C00) { op_muldivl(op); return; }
    if ((op & 0xFF00) == 0x4A00 && (op & 0xC0) != 0xC0) { int sz = sz_from2((op >> 6) & 3); EA e = get_ea(mode, reg, sz); logic(ea_rd(&e, sz), sz); return; } /* tst */
    {
        int kind = (op >> 8) & 0xF, sz = sz_from2((op >> 6) & 3);
        if (sz && (kind == 2 || kind == 4 || kind == 6 || kind == 0)) {
            EA e = get_ea(mode, reg, sz);
            u32 v = ea_rd(&e, sz), r;
            if (kind == 2) { r = 0; ea_wr(&e, sz, 0); logic(0, sz); return; }                 /* clr */
            if (kind == 4) { r = do_sub(0, v, sz, 0, 1); ea_wr(&e, sz, r); return; }          /* neg */
            if (kind == 6) { r = ~v & msk(sz); ea_wr(&e, sz, r); logic(r, sz); return; }      /* not */
            if (kind == 0) { r = do_sub(0, v, sz, cpu.x, 1); ea_wr(&e, sz, r); return; }      /* negx (approx Z) */
        }
    }
    die("unknown opcode %04x (group 4)", op);
}

static void op_q(u16 op)
{
    int mode = (op >> 3) & 7, reg = op & 7, s = (op >> 6) & 3;
    if (s == 3) {
        int cc = (op >> 8) & 15;
        if (mode == 1) {  /* dbcc */
            s16 d = (s16)fetch16();
            if (!cond(cc)) {
                u16 w = (u16)(cpu.d[reg] - 1);
                cpu.d[reg] = (cpu.d[reg] & 0xFFFF0000) | w;
                if (w != 0xFFFF) cpu.pc = cpu.pc - 2 + (u32)(s32)d;
            }
            return;
        }
        { EA e = get_ea(mode, reg, 1); ea_wr(&e, 1, cond(cc) ? 0xFF : 0); }
        return;
    }
    {
        int sz = sz_from2(s);
        u32 q = (op >> 9) & 7;
        if (!q) q = 8;
        if (mode == 1) { if (op & 0x100) cpu.a[reg] -= q; else cpu.a[reg] += q; return; }
        {
            EA e = get_ea(mode, reg, sz);
            u32 v = ea_rd(&e, sz);
            ea_wr(&e, sz, (op & 0x100) ? do_sub(v, q, sz, 0, 1) : do_add(v, q, sz, 0));
        }
    }
}

static void op_branch(u16 op)
{
    int cc = (op >> 8) & 15;
    u32 base = cpu.pc;
    s32 d = (s8)(op & 0xFF);
    if ((op & 0xFF) == 0) d = (s16)fetch16();
    else if ((op & 0xFF) == 0xFF) d = (s32)fetch32();
    if (cc == 1) { push32(cpu.pc); cpu.pc = base + (u32)d; return; }  /* bsr */
    if (cond(cc)) cpu.pc = base + (u32)d;
}

static void op_arith(u16 op)   /* 8 or, 9 sub, B cmp/eor, C and/mul, D add */
{
    int g = op >> 12, dn = (op >> 9) & 7, opm = (op >> 6) & 7, mode = (op >> 3) & 7, reg = op & 7;
    if (opm == 3 || opm == 7) {
        if (g == 0x9 || g == 0xD || g == 0xB) {   /* suba/adda/cmpa */
            int sz = opm == 3 ? 2 : 4;
            EA e = get_ea(mode, reg, sz);
            u32 v = (u32)sext(ea_rd(&e, sz), sz);
            if (g == 0x9) cpu.a[dn] -= v;
            else if (g == 0xD) cpu.a[dn] += v;
            else do_sub(cpu.a[dn], v, 4, 0, 0);
            return;
        }
        if (g == 0xC) {   /* mulu.w / muls.w */
            EA e = get_ea(mode, reg, 2);
            u32 v = ea_rd(&e, 2);
            u32 r = opm == 3 ? (cpu.d[dn] & 0xFFFF) * v : (u32)((s32)(s16)cpu.d[dn] * (s32)(s16)v);
            cpu.d[dn] = r; logic(r, 4);
            return;
        }
        if (g == 0x8) {   /* divu.w / divs.w */
            EA e = get_ea(mode, reg, 2);
            u32 v = ea_rd(&e, 2);
            if (!v) die("division by zero (word)");
            if (opm == 3) {
                u32 q = cpu.d[dn] / v, r = cpu.d[dn] % v;
                if (q > 0xFFFF) { cpu.v = 1; return; }
                cpu.d[dn] = (r << 16) | q; cpu.v = 0; cpu.c = 0; setnz(q, 2);
            } else {
                s32 a = (s32)cpu.d[dn], b = (s16)v, q = a / b, r = a % b;
                if (q > 32767 || q < -32768) { cpu.v = 1; return; }
                cpu.d[dn] = ((u32)r << 16) | ((u32)q & 0xFFFF); cpu.v = 0; cpu.c = 0; setnz((u32)q, 2);
            }
            return;
        }
    }
    {
        int sz = sz_from2(opm & 3), toEa = (opm >> 2) & 1;
        if ((g == 0x9 || g == 0xD) && toEa && (mode == 0 || mode == 1)) {   /* subx/addx */
            if (mode == 1) die("addx/subx -(An) not supported");
            {
                u32 oldz = cpu.z, r = g == 0x9 ? do_sub(cpu.d[dn], cpu.d[reg], sz, cpu.x, 1) : do_add(cpu.d[dn], cpu.d[reg], sz, cpu.x);
                if (r == 0) cpu.z = oldz;
                cpu.d[dn] = (cpu.d[dn] & ~msk(sz)) | r;
            }
            return;
        }
        if (g == 0xC && toEa && (mode == 0 || mode == 1) && (opm == 5 || opm == 6)) {   /* exg */
            u32 *x, *y, t;
            if (opm == 5 && mode == 0) { x = &cpu.d[dn]; y = &cpu.d[reg]; }
            else if (opm == 5) { x = &cpu.a[dn]; y = &cpu.a[reg]; }
            else { x = &cpu.d[dn]; y = &cpu.a[reg]; }
            t = *x; *x = *y; *y = t; return;
        }
        if (g == 0xB && toEa && mode == 1) die("cmpm not supported");
        {
            EA e = get_ea(mode, reg, sz);
            u32 v = ea_rd(&e, sz), d = cpu.d[dn] & msk(sz), r;
            if (g == 0xB) {
                if (!toEa) { do_sub(d, v, sz, 0, 0); return; }          /* cmp */
                r = v ^ d; logic(r, sz); ea_wr(&e, sz, r); return;       /* eor */
            }
            if (!toEa) {
                switch (g) {
                case 0x8: r = d | v; logic(r, sz); break;
                case 0x9: r = do_sub(d, v, sz, 0, 1); break;
                case 0xC: r = d & v; logic(r, sz); break;
                default: r = do_add(d, v, sz, 0); break;
                }
                cpu.d[dn] = (cpu.d[dn] & ~msk(sz)) | r;
            } else {
                switch (g) {
                case 0x8: r = v | d; logic(r, sz); break;
                case 0x9: r = do_sub(v, d, sz, 0, 1); break;
                case 0xC: r = v & d; logic(r, sz); break;
                default: r = do_add(v, d, sz, 0); break;
                }
                ea_wr(&e, sz, r);
            }
        }
    }
}

static void op_shift(u16 op)
{
    int sz = sz_from2((op >> 6) & 3);
    u32 v, cnt, m, sb;
    int type, left, i, reg = op & 7;
    EA e;
    if (!sz) {   /* memory form, shift by 1 */
        sz = 2; type = (op >> 9) & 3; left = (op >> 8) & 1; cnt = 1;
        e = get_ea((op >> 3) & 7, reg, 2);
    } else {
        type = (op >> 3) & 3; left = (op >> 8) & 1;
        cnt = (op >> 9) & 7;
        if (op & 0x20) cnt = cpu.d[cnt] & 63; else if (!cnt) cnt = 8;
        e.kind = 0; e.reg = reg; e.addr = 0;
    }
    m = msk(sz); sb = sgn(sz);
    v = ea_rd(&e, sz);
    cpu.v = 0;
    if (cnt == 0) { cpu.c = (type == 2) ? cpu.x : 0; setnz(v, sz); return; }
    for (i = 0; i < (int)cnt; i++) {
        u32 out;
        if (left) {
            out = (v & sb) != 0;
            if (type == 0 && (((v << 1) & sb) != (v & sb))) cpu.v = 1;
            v = (v << 1) & m;
            if (type == 2) v |= (u32)cpu.x;
            else if (type == 3) v |= out;
        } else {
            out = v & 1;
            if (type == 0) v = (v >> 1) | (v & sb);
            else if (type == 1) v = v >> 1;
            else if (type == 2) v = (v >> 1) | (cpu.x ? sb : 0);
            else v = (v >> 1) | (out ? sb : 0);
        }
        cpu.c = (int)out;
        if (type != 3) cpu.x = (int)out;
    }
    ea_wr(&e, sz, v);
    setnz(v, sz);
}

u32 trace_lo, trace_hi; int trace_frame = -1; extern int sim_frames(void);
void cpu_step(void)
{
    u16 op;
    cpu.lastpc = cpu.pc;
    if (cpu.pc >= trace_lo && cpu.pc < trace_hi && (trace_frame < 0 || trace_frame == sim_frames())) {
        int i;
        fprintf(stderr, "%08x: %04x %04x %04x |", cpu.pc, rd16(cpu.pc), rd16(cpu.pc + 2), rd16(cpu.pc + 4));
        for (i = 0; i < 8; i++) fprintf(stderr, " d%d=%x", i, cpu.d[i]);
        fprintf(stderr, " a0=%x a1=%x sp=%x NZVC=%d%d%d%d\n", cpu.a[0], cpu.a[1], cpu.a[7], cpu.n, cpu.z, cpu.v, cpu.c);
    }
    op = fetch16();
    switch (op >> 12) {
    case 0x0: op_imm(op); break;
    case 0x1: case 0x2: case 0x3: op_move(op); break;
    case 0x4: op_misc(op); break;
    case 0x5: op_q(op); break;
    case 0x6: op_branch(op); break;
    case 0x7: cpu.d[(op >> 9) & 7] = (u32)(s32)(s8)(op & 0xFF); logic(cpu.d[(op >> 9) & 7], 4); break;
    case 0xA: os_call(op); break;
    case 0xE: op_shift(op); break;
    case 0x8: case 0x9: case 0xB: case 0xC: case 0xD: op_arith(op); break;
    default: die("unknown opcode %04x", op);
    }
    cpu.steps++;
}

/* run until PC reaches 'until' (a return sentinel) */
void cpu_run_until(u32 until)
{
    while (cpu.pc != until) cpu_step();
}
