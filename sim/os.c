/* Fake AmigaOS + chipset around the 68020 interpreter, with a scripted test run.
   Usage: m68ksim <hunk-exe> <script> <outdir> <rootdir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include "sim.h"

#define CHIP_SIZE (2u << 20)
#define FAST_BASE 0x40000000u
#define FAST_SIZE (64u << 20)
#define SYSBASE 0x2000u
#define DOSBASE 0x3000u
#define GFXBASE 0x4000u
#define INTBASE 0x5000u
#define PROC    0x7000u
#define RET_TOP 0x0F00u
#define RET_NEST 0x0F10u
#define INT_STACK 0x3F000u

static u8 *chip, *fast;
static u16 custom[0x100];
static u32 intreq, dmacon, audLC[4], audLatched[4], cop1lc;
static double audPos;          /* frames played in current block */
static int audRunning;
static char outdir[512], rootdir[512];
static FILE *wav; static u32 wavFrames;
static int frames, maxFrames = 100000;
int sim_frames(void) { return frames; }
static u32 vbCode, vbData, inCode, inData;
static int nesting;

void die(const char *fmt, ...)
{
    va_list ap; int i;
    fprintf(stderr, "\n*** SIM ERROR at PC %08x (op at %08x), frame %d: ", cpu.pc, cpu.lastpc, frames);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n D:"); for (i = 0; i < 8; i++) fprintf(stderr, " %08x", cpu.d[i]);
    fprintf(stderr, "\n A:"); for (i = 0; i < 8; i++) fprintf(stderr, " %08x", cpu.a[i]);
    fprintf(stderr, "\n");
    exit(2);
}

/* ---------------- memory ---------------- */
static u8 *ptr(u32 a, int n, int write)
{
    if (a < CHIP_SIZE) {
        if (a < 0x400 && !(a >= 4 && a + n <= 8 && !write) && !(a >= RET_TOP && a < RET_TOP + 0x40))
            die("%s at low address %08x (null pointer?)", write ? "write" : "read", a);
        if (a + n > CHIP_SIZE) die("chip overrun %08x", a);
        return chip + a;
    }
    if (a >= FAST_BASE && a + n <= FAST_BASE + FAST_SIZE) return fast + (a - FAST_BASE);
    return NULL;
}

static u32 beam;
static u16 custom_rd(u32 off)
{
    switch (off) {
    case 0x002: return (u16)dmacon;
    case 0x004: beam += 3; return (u16)((beam / 227 / 256) & 1);
    case 0x006: return (u16)((((beam / 227) & 0xFF) << 8) | (beam % 227));
    case 0x01E: return (u16)intreq;
    }
    return 0;
}
static void custom_wr(u32 off, u16 v)
{
    custom[off / 2] = v;
    if (off == 0x096) { if (v & 0x8000) dmacon |= v & 0x7FFF; else dmacon &= ~(u32)v; }
    else if (off == 0x09C) { if (v & 0x8000) intreq |= v & 0x7FFF; else intreq &= ~(u32)v; }
    else if (off >= 0x0A0 && off < 0x0E0 && ((off - 0x0A0) & 0xF) < 4) {
        int ch = (off - 0x0A0) >> 4;
        if (((off - 0x0A0) & 0xF) == 0) audLC[ch] = (audLC[ch] & 0xFFFF) | ((u32)v << 16);
        else audLC[ch] = (audLC[ch] & 0xFFFF0000) | v;
    } else if (off == 0x080) cop1lc = (cop1lc & 0xFFFF) | ((u32)v << 16);
    else if (off == 0x082) cop1lc = (cop1lc & 0xFFFF0000) | v;
}

static u8 ciaPRBval(void) { static int t; t++; return (u8)(0x80 + (int)(60 * ((t % 64) < 32 ? 1 : -1))); }

u8 rd8(u32 a)
{
    u8 *p = ptr(a, 1, 0);
    if (p) return *p;
    if (a == 0xBFE001) return 0xFF;            /* CIAA PRA: no mouse button */
    if (a == 0xBFE101) return ciaPRBval();
    if (a >= 0xBFD000 && a < 0xBFF000) return 0;
    if (a >= 0xDFF000 && a < 0xDFF200) { u16 v = custom_rd((a - 0xDFF000) & ~1u); return (a & 1) ? (u8)v : (u8)(v >> 8); }
    die("read8 from unmapped %08x", a); return 0;
}
u16 rd16(u32 a)
{
    u8 *p = ptr(a, 2, 0);
    if (p) return (u16)((p[0] << 8) | p[1]);
    if (a >= 0xDFF000 && a < 0xDFF200) return custom_rd(a - 0xDFF000);
    die("read16 from unmapped %08x", a); return 0;
}
u32 rd32(u32 a)
{
    u8 *p = ptr(a, 4, 0);
    if (p) return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
    if (a >= 0xDFF000 && a < 0xDFF200) return ((u32)custom_rd(a - 0xDFF000) << 16) | custom_rd(a - 0xDFF000 + 2);
    die("read32 from unmapped %08x", a); return 0;
}
static u32 watch;
static void chkw(u32 a, int n, u32 v) { if (watch && a <= watch && watch < a + (u32)n) fprintf(stderr, "[f%d] WATCH write %08x (%d bytes) = %x at PC %08x stack %08x %08x %08x %08x src a0=%08x a1=%08x\n", frames, a, n, v, cpu.lastpc, rd32(cpu.a[7]), rd32(cpu.a[7]+4), rd32(cpu.a[7]+8), rd32(cpu.a[7]+12), cpu.a[0], cpu.a[1]); }
void wr8(u32 a, u32 v)
{
    chkw(a, 1, v);
    u8 *p = ptr(a, 1, 1);
    if (p) { *p = (u8)v; return; }
    if (a >= 0xBFD000 && a < 0xBFF000) return;   /* CIA writes ignored */
    die("write8 to unmapped %08x", a);
}
void wr16(u32 a, u32 v)
{
    chkw(a, 2, v);
    u8 *p = ptr(a, 2, 1);
    if (p) { p[0] = (u8)(v >> 8); p[1] = (u8)v; return; }
    if (a >= 0xDFF000 && a < 0xDFF200) { custom_wr(a - 0xDFF000, (u16)v); return; }
    die("write16 to unmapped %08x", a);
}
void wr32(u32 a, u32 v)
{
    chkw(a, 4, v);
    u8 *p = ptr(a, 4, 1);
    if (p) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; return; }
    if (a >= 0xDFF000 && a < 0xDFF200) { custom_wr(a - 0xDFF000, (u16)(v >> 16)); custom_wr(a - 0xDFF000 + 2, (u16)v); return; }
    die("write32 to unmapped %08x", a);
}

static void cstr(u32 a, char *out, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) { out[i] = (char)rd8(a + (u32)i); if (!out[i]) return; }
    out[n - 1] = 0;
}

/* ---------------- allocator (tracked) ---------------- */
#define MAXALLOC 20000
static struct { u32 addr, size; int live, chip; } allocs[MAXALLOC];
static int nallocs;
static u32 chipTop = 0x40000, fastTop = FAST_BASE + 0x100000;
static u32 alloc_mem(u32 size, u32 flags)
{
    int chipReq = (flags & 2) != 0;
    u32 a;
    size = (size + 15) & ~15u;
    if (chipReq) { a = chipTop; chipTop += size + 16; if (chipTop > CHIP_SIZE) return 0; }
    else { a = fastTop; fastTop += size + 16; if (fastTop > FAST_BASE + FAST_SIZE) return 0; }
    memset(ptr(a, (int)size, 1), (flags & 0x10000) ? 0 : 0xA5, size);
    if (nallocs >= MAXALLOC) die("too many allocations");
    allocs[nallocs].addr = a; allocs[nallocs].size = size; allocs[nallocs].live = 1; allocs[nallocs].chip = chipReq;
    nallocs++;
    return a;
}
static void free_mem(u32 a)
{
    int i;
    if (!a) return;
    for (i = nallocs - 1; i >= 0; i--)
        if (allocs[i].addr == a) {
            if (!allocs[i].live) die("double free %08x", a);
            allocs[i].live = 0;
            memset(ptr(a, (int)allocs[i].size, 1), 0xDD, allocs[i].size);
            return;
        }
    die("FreeVec of unknown pointer %08x", a);
}

/* ---------------- host files ---------------- */
#define MAXFH 64
static FILE *fhs[MAXFH];
static struct { char path[600]; DIR *d; int used; } locks[MAXFH];

static void host_path(u32 amigaName, char *out)
{
    char n[300], *p;
    cstr(amigaName, n, sizeof n);
    p = strchr(n, ':');
    snprintf(out, 600, "%s/%s", rootdir, p ? p + 1 : n);
}

/* ---------------- nested calls (interrupts, handlers) ---------------- */
static u32 call_nested(u32 code, u32 a0, u32 a1)
{
    Cpu save = cpu;
    u32 r;
    nesting++;
    cpu.a[7] = INT_STACK - (u32)nesting * 0x4000;
    cpu.a[7] -= 4; wr32(cpu.a[7], RET_NEST);
    cpu.a[0] = a0; cpu.a[1] = a1; cpu.a[5] = code;
    cpu.pc = code;
    cpu_run_until(RET_NEST);
    r = cpu.d[0];
    save.steps = cpu.steps;
    cpu = save;
    nesting--;
    return r;
}

/* ---------------- script ---------------- */
typedef struct { int frame; char kind[16]; int code, qual, x, y; char arg[200]; } Act;
static Act acts[4096]; static int nacts, actPos;
static u32 evbuf = 0;  /* InputEvent scratch in chip */

static void send_input(int cls, int code, int qual, int dx, int dy)
{
    u32 ie = evbuf;
    int i;
    for (i = 0; i < 22; i++) wr8(ie + (u32)i, 0);
    wr8(ie + 4, (u32)cls); wr16(ie + 6, (u32)code); wr16(ie + 8, (u32)qual);
    wr16(ie + 10, (u32)(u16)(s16)dx); wr16(ie + 12, (u32)(u16)(s16)dy);
    if (!inCode) die("input event but no input handler installed");
    if (call_nested(inCode, ie, inData) != ie) die("input handler returned wrong list");
    if (rd8(ie + 4) != 0) die("input handler didn't swallow the event");
}

static void screenshot(const char *name)
{
    u32 pc = cop1lc, planes[6] = {0}, bplcon0 = 0, pal[32];
    char path[700];
    FILE *f;
    int x, y, np, guard = 0, i;
    for (i = 0; i < 32; i++) pal[i] = 0;
    if (!pc) die("screenshot: no copper list");
    for (;;) {
        u16 r = rd16(pc), v = rd16(pc + 2);
        pc += 4;
        if (r == 0xFFFF && v == 0xFFFE) break;
        if (++guard > 500) die("copper list doesn't terminate");
        if (r & 1) die("copper WAIT/SKIP not expected");
        if (r >= 0xE0 && r < 0xF8) { int k = (r - 0xE0) / 4; if (r & 2) planes[k] = (planes[k] & 0xFFFF0000) | v; else planes[k] = (planes[k] & 0xFFFF) | ((u32)v << 16); }
        if (r == 0x100) bplcon0 = v;
        if (r >= 0x180 && r < 0x1C0) pal[(r - 0x180) / 2] = v;
    }
    np = (int)((bplcon0 >> 12) & 7);
    if (!(bplcon0 & 0x8000)) die("screen is not hires");
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    fprintf(f, "P6 640 512 255\n");
    for (y = 0; y < 512; y++)
        for (x = 0; x < 640; x++) {
            int c = 0, k; u8 rgb[3]; u32 col;
            for (k = 0; k < np; k++) if (chip[planes[k] + (u32)(y / 2) * 80 + (u32)(x >> 3)] & (0x80 >> (x & 7))) c |= 1 << k;
            col = pal[c];
            rgb[0] = (u8)(((col >> 8) & 15) * 17); rgb[1] = (u8)(((col >> 4) & 15) * 17); rgb[2] = (u8)((col & 15) * 17);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("  [frame %d] screenshot %s\n", frames, name);
}

static void wav_block(void)
{
    int i;
    for (i = 0; i < 1024; i++) {
        s16 l = (s16)((s8)chip[audLatched[0] + (u32)i] * 256 + chip[audLatched[3] + (u32)i] * 4);
        s16 r = (s16)((s8)chip[audLatched[1] + (u32)i] * 256 + chip[audLatched[2] + (u32)i] * 4);
        fwrite(&l, 2, 1, wav); fwrite(&r, 2, 1, wav);
        wavFrames++;
    }
}

static void audio_advance(double nframes)
{
    int on = (dmacon & 0x200) && (dmacon & 0xF) == 0xF;
    if (!on) { audRunning = 0; return; }
    if (!audRunning) {   /* DMA just started: latch and raise interrupt */
        int c;
        if (custom[(0xA4) / 2] != 512) die("audio length is not 512 words");
        for (c = 0; c < 4; c++) audLatched[c] = audLC[c];
        intreq |= 0x0780; audRunning = 1; audPos = 0;
        return;
    }
    audPos += nframes;
    while (audPos >= 1024) {
        int c;
        audPos -= 1024;
        wav_block();                      /* the block that just finished */
        for (c = 0; c < 4; c++) audLatched[c] = audLC[c];
        if (intreq & 0x0080) printf("  [frame %d] WARNING: audio interrupt not serviced in time\n", frames);
        intreq |= 0x0780;
    }
}

static void do_frame(void)
{
    frames++;
    if (frames > maxFrames) die("timeout: program didn't quit");
    /* scripted actions */
    while (actPos < nacts && acts[actPos].frame <= frames) {
        Act *a = &acts[actPos++];
        if (!strcmp(a->kind, "key")) { send_input(1, a->code, a->qual, 0, 0); send_input(1, a->code | 0x80, a->qual, 0, 0); }
        else if (!strcmp(a->kind, "keydown")) send_input(1, a->code, a->qual, 0, 0);
        else if (!strcmp(a->kind, "move")) send_input(2, 0xFF, 0, a->x, a->y);
        else if (!strcmp(a->kind, "lmb")) send_input(2, a->code ? 0x68 : 0xE8, 0, 0, 0);
        else if (!strcmp(a->kind, "shot")) screenshot(a->arg);
        else if (!strcmp(a->kind, "type")) {
            const char *s = a->arg;
            static const char r1[] = "qwertyuiop", r2[] = "asdfghjkl", r3[] = "zxcvbnm", r0[] = "`1234567890";
            for (; *s; s++) {
                int c = *s, code = -1, q = 0; const char *f;
                if (c >= 'A' && c <= 'Z') { c += 32; q = 1; }
                if ((f = strchr(r1, c))) code = 0x10 + (int)(f - r1);
                else if ((f = strchr(r2, c))) code = 0x20 + (int)(f - r2);
                else if ((f = strchr(r3, c))) code = 0x31 + (int)(f - r3);
                else if ((f = strchr(r0, c))) code = (int)(f - r0);
                else if (c == '.') code = 0x39;
                else if (c == '-') code = 0x0B;
                else if (c == ' ') code = 0x40;
                if (code >= 0) { send_input(1, code, q, 0, 0); send_input(1, code | 0x80, q, 0, 0); }
            }
        }
    }
    audio_advance(28604.0 / 50.0);
    if (vbCode) call_nested(vbCode, 0, vbData);
}

/* ---------------- OS calls ---------------- */
static void ret(u32 v) { cpu.d[0] = v; }

static void exec_call(int off)
{
    switch (off) {
    case 684: ret(alloc_mem(cpu.d[0], cpu.d[1])); break;            /* AllocVec */
    case 690: free_mem(cpu.a[1]); break;                              /* FreeVec */
    case 552: { char n[64]; cstr(cpu.a[1], n, 64);                    /* OpenLibrary */
        ret(!strcmp(n, "dos.library") ? DOSBASE : !strcmp(n, "graphics.library") ? GFXBASE : !strcmp(n, "intuition.library") ? INTBASE : 0); break; }
    case 414: break;                                                  /* CloseLibrary */
    case 294: if (cpu.a[1]) die("FindTask(name)"); ret(PROC); break;
    case 330: ret(16); break;                                         /* AllocSignal */
    case 336: break;
    case 324: break;                                                  /* Signal */
    case 318: if (nesting) die("Wait() inside an interrupt"); do_frame(); ret(cpu.d[0]); break;
    case 132: case 138: case 120: case 126: break;                    /* Forbid Permit Disable Enable */
    case 384: break;                                                  /* WaitPort */
    case 372: ret(0); break;                                          /* GetMsg */
    case 378: break;
    case 168: if (cpu.d[0] != 5) die("AddIntServer %d", cpu.d[0]);
        vbCode = rd32(cpu.a[1] + 18); vbData = rd32(cpu.a[1] + 14); break;
    case 174: vbCode = 0; break;
    case 666: ret(alloc_mem(34, 0x10000)); break;                     /* CreateMsgPort */
    case 672: free_mem(cpu.a[0]); break;
    case 654: ret(alloc_mem(cpu.d[0], 0x10000)); break;               /* CreateIORequest */
    case 660: free_mem(cpu.a[0]); break;
    case 444: { char n[64]; cstr(cpu.a[0], n, 64); if (strcmp(n, "input.device")) die("OpenDevice %s", n); ret(0); break; }
    case 450: break;
    case 456: {                                                       /* DoIO */
        u16 cmd = rd16(cpu.a[1] + 28); u32 data = rd32(cpu.a[1] + 40);
        if (cmd == 9) { inCode = rd32(data + 18); inData = rd32(data + 14); if (rd8(data + 9) != 100) die("handler pri"); }
        else if (cmd == 10) inCode = 0;
        else die("DoIO cmd %d", cmd);
        ret(0); break; }
    case 636: break;                                                  /* CacheClearU */
    case 732: {                                                       /* StackSwap */
        u32 ss = cpu.a[0], retaddr = rd32(cpu.a[7]), newsp = rd32(ss + 8);
        wr32(ss + 8, cpu.a[7] + 4);
        cpu.a[7] = newsp - 4; wr32(cpu.a[7], retaddr);
        break; }
    default: die("exec LVO -%d not emulated", off);
    }
}

static void dos_call(int off)
{
    char hp[600];
    switch (off) {
    case 30: {   /* Open */
        int i;
        host_path(cpu.d[1], hp);
        for (i = 1; i < MAXFH && fhs[i]; i++) ;
        fhs[i] = fopen(hp, cpu.d[2] == 1006 ? "wb" : "rb");
        ret(fhs[i] ? (u32)i : 0);
        break; }
    case 36: if (cpu.d[1] < MAXFH && fhs[cpu.d[1]]) { fclose(fhs[cpu.d[1]]); fhs[cpu.d[1]] = 0; } ret(1); break;
    case 42: { FILE *f = fhs[cpu.d[1]]; u32 n = cpu.d[3], i, got = 0; int c;
        for (i = 0; i < n && (c = fgetc(f)) != EOF; i++) { wr8(cpu.d[2] + i, (u32)c); got++; }
        ret(got); break; }
    case 48: { FILE *f = fhs[cpu.d[1]]; u32 n = cpu.d[3], i;
        for (i = 0; i < n; i++) fputc(rd8(cpu.d[2] + i), f);
        ret(n); break; }
    case 66: { FILE *f = fhs[cpu.d[1]]; long old = ftell(f);
        fseek(f, (long)(s32)cpu.d[2], (s32)cpu.d[3] == -1 ? SEEK_SET : (s32)cpu.d[3] == 0 ? SEEK_CUR : SEEK_END);
        ret((u32)old); break; }
    case 84: { int i; struct stat st;
        host_path(cpu.d[1], hp);
        if (stat(hp, &st)) { ret(0); break; }
        for (i = 1; i < MAXFH && locks[i].used; i++) ;
        locks[i].used = 1; strcpy(locks[i].path, hp); locks[i].d = 0;
        ret((u32)i); break; }
    case 90: if (locks[cpu.d[1]].d) closedir(locks[cpu.d[1]].d); locks[cpu.d[1]].used = 0; break;
    case 102: { struct stat st; u32 fib = cpu.d[2];
        stat(locks[cpu.d[1]].path, &st);
        wr32(fib + 4, S_ISDIR(st.st_mode) ? 2 : (u32)-3);
        if (locks[cpu.d[1]].d) closedir(locks[cpu.d[1]].d);
        locks[cpu.d[1]].d = S_ISDIR(st.st_mode) ? opendir(locks[cpu.d[1]].path) : 0;
        ret(1); break; }
    case 108: { struct dirent *de; u32 fib = cpu.d[2]; DIR *d = locks[cpu.d[1]].d; int i;
        while (d && (de = readdir(d)) && de->d_name[0] == '.') ;
        if (!d || !de) { ret(0); break; }
        { struct stat st; char full[900]; snprintf(full, sizeof full, "%s/%s", locks[cpu.d[1]].path, de->d_name); stat(full, &st);
          wr32(fib + 4, S_ISDIR(st.st_mode) ? 2 : (u32)-3); wr32(fib + 124, (u32)st.st_size); }
        for (i = 0; i < 107 && de->d_name[i]; i++) wr8(fib + 8 + (u32)i, (u32)de->d_name[i]);
        wr8(fib + 8 + (u32)i, 0);
        ret(1); break; }
    case 228: if (cpu.d[1] != 2) die("AllocDosObject %d", cpu.d[1]); ret(alloc_mem(260, 0x10000)); break;
    case 234: free_mem(cpu.d[2]); break;
    default: die("dos LVO -%d not emulated", off);
    }
}

void os_call(u16 op)
{
    int lib = (op >> 8) & 0xF, off = (op & 0xFF) * 6;
    if (getenv("OSTRACE") && !(lib == 0 && (off == 120 || off == 126))) fprintf(stderr, "[f%d] lib%d -%d d0=%x d1=%x d2=%x d3=%x a0=%x a1=%x ret=%x\n", frames, lib, off, cpu.d[0], cpu.d[1], cpu.d[2], cpu.d[3], cpu.a[0], cpu.a[1], rd32(cpu.a[7]));
    switch (lib) {
    case 0: exec_call(off); break;
    case 1: dos_call(off); break;
    case 2: if (off != 222 && off != 270) die("graphics LVO -%d", off); break;
    case 3: if (off != 390) die("intuition LVO -%d", off); break;
    default: die("bad line-A %04x", op);
    }
    cpu.pc = rd32(cpu.a[7]); cpu.a[7] += 4;   /* rts */
}

static void make_lib(u32 base, int libno, int maxoff)
{
    int off;
    for (off = 6; off <= maxoff; off += 6) wr16(base - (u32)off, 0xA000u | ((u32)libno << 8) | (u32)(off / 6));
}

/* ---------------- loader ---------------- */
static u32 load_hunk(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n; u8 *d; u32 pos = 0, base = 0, nlongs;
#define L() (pos += 4, ((u32)d[pos-4] << 24) | ((u32)d[pos-3] << 16) | ((u32)d[pos-2] << 8) | d[pos-1])
    if (!f) die("can't open %s", path);
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc((size_t)n); if (fread(d, 1, (size_t)n, f) != (size_t)n) die("read"); fclose(f);
    if (L() != 0x3F3) die("not a hunk file");
    if (L() != 0) die("resident libs");
    if (L() != 1 || L() != 0 || L() != 0) die("expected exactly one hunk");
    nlongs = L();
    base = alloc_mem(nlongs * 4, 0);
    if (L() != 0x3E9) die("expected HUNK_CODE");
    if (L() != nlongs) die("hunk size mismatch");
    memcpy(ptr(base, (int)(nlongs * 4), 1), d + pos, nlongs * 4); pos += nlongs * 4;
    for (;;) {
        u32 t = L();
        if (t == 0x3F2) break;
        if (t == 0x3EC) {
            u32 cnt;
            while ((cnt = L()) != 0) {
                u32 h = L(), i;
                if (h != 0) die("reloc to other hunk");
                for (i = 0; i < cnt; i++) { u32 o = L(); wr32(base + o, rd32(base + o) + base); }
            }
        } else die("unexpected hunk type %x", t);
    }
    free(d);
    return base;
#undef L
}

int main(int argc, char **argv)
{
    u32 entry;
    FILE *sf;
    char line[300];
    int i, leaks = 0;
    u32 hdr[11];
    if (argc < 5) { fprintf(stderr, "usage: m68ksim exe script outdir rootdir\n"); return 1; }
    strcpy(outdir, argv[3]); strcpy(rootdir, argv[4]);
    chip = calloc(1, CHIP_SIZE); fast = calloc(1, FAST_SIZE);
    /* script */
    sf = fopen(argv[2], "r");
    while (sf && fgets(line, sizeof line, sf)) {
        Act *a = &acts[nacts];
        char *p = line;
        if (line[0] == '#' || line[0] == '\n') continue;
        memset(a, 0, sizeof *a);
        if (sscanf(p, "%d %15s", &a->frame, a->kind) < 2) continue;
        p = strchr(p, ' ') + 1; p = strchr(p, ' '); p = p ? p + 1 : (char *)"";
        if (!strcmp(a->kind, "shot") || !strcmp(a->kind, "type")) { sscanf(p, "%199[^\n]", a->arg); }
        else if (!strcmp(a->kind, "move")) sscanf(p, "%d %d", &a->x, &a->y);
        else if (!strcmp(a->kind, "maxframes")) { maxFrames = a->frame; continue; }
        else sscanf(p, "%x %x", (unsigned *)&a->code, (unsigned *)&a->qual);
        nacts++;
    }
    if (sf) fclose(sf);
    /* OS structures */
    chip[4] = 0; chip[5] = 0; chip[6] = (u8)(SYSBASE >> 8); chip[7] = (u8)SYSBASE;
    make_lib(SYSBASE, 0, 800); make_lib(DOSBASE, 1, 400); make_lib(GFXBASE, 2, 400); make_lib(INTBASE, 3, 400);
    wr16(SYSBASE + 296, 0x8F);               /* AttnFlags: 010/020/030/040 + FPU */
    wr32(GFXBASE + 34, 0x6000); wr32(GFXBASE + 38, 0x6100);
    wr32(PROC + 172, 0x1234);                 /* pr_CLI: started from shell */
    wr32(PROC + 184, 0);
    evbuf = alloc_mem(64, 0x10002);
    {
        char wp[700]; snprintf(wp, sizeof wp, "%s/audio.wav", outdir);
        wav = fopen(wp, "wb"); memset(hdr, 0, sizeof hdr); fwrite(hdr, 4, 11, wav);
    }
    if (getenv("WATCH")) sscanf(getenv("WATCH"), "%x", &watch);
    entry = load_hunk(argv[1]);
    if (getenv("TRACE")) { extern u32 trace_lo, trace_hi; sscanf(getenv("TRACE"), "%x-%x", &trace_lo, &trace_hi); trace_lo += entry; trace_hi += entry; if (getenv("TRACEFRAME")) { extern int trace_frame; trace_frame = atoi(getenv("TRACEFRAME")); } }
    printf("loaded at %08x, running...\n", entry);
    cpu.a[7] = 0xF000; cpu.a[7] -= 4; wr32(cpu.a[7], RET_TOP);
    cpu.pc = entry;
    cpu_run_until(RET_TOP);
    printf("exited with code %d after %d frames, %llu instructions\n", (int)cpu.d[0], frames, (unsigned long long)cpu.steps);
    for (i = 0; i < nallocs; i++) if (allocs[i].live) leaks++;
    printf("allocations: %d, still live at exit: %d (expected 2: program image + test buffer)\n", nallocs, leaks);
    {   /* finish wav */
        u32 dl = wavFrames * 4, rate = 28604;
        u8 h[44]; memcpy(h, "RIFF", 4);
        h[4] = (u8)(dl + 36); h[5] = (u8)((dl + 36) >> 8); h[6] = (u8)((dl + 36) >> 16); h[7] = (u8)((dl + 36) >> 24);
        memcpy(h + 8, "WAVEfmt ", 8); h[16] = 16; h[17] = h[18] = h[19] = 0; h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
        h[24] = (u8)rate; h[25] = (u8)(rate >> 8); h[26] = 0; h[27] = 0;
        h[28] = (u8)(rate * 4); h[29] = (u8)((rate * 4) >> 8); h[30] = (u8)((rate * 4) >> 16); h[31] = 0;
        h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0; memcpy(h + 36, "data", 4);
        h[40] = (u8)dl; h[41] = (u8)(dl >> 8); h[42] = (u8)(dl >> 16); h[43] = (u8)(dl >> 24);
        fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav);
    }
    return (cpu.d[0] == 0 && leaks <= 2) ? 0 : 1;
}
