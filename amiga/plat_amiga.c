/* AmigaOS 3.x platform layer for Paula's Revenge 1200.
   - system-friendly display takeover (own copper list, OS keeps running)
   - input.device handler for keyboard and mouse
   - 14-bit stereo Paula output, mixed in the vertical-blank interrupt
   - dos.library file access, parallel port sampler
   NO global or static variables: all state lives in Plat / App. */
#include "amiga.h"
#include "../src/app.h"

#define PLANE_BYTES (ROWBYTES * SCREEN_H)
#define STACK_SIZE (96 * 1024)

typedef struct Plat {
    void *sys, *dos, *gfx, *intui;
    App *app;
    void *task;
    u32 frameSig;
    void *oldView, *oldWinPtr;
    u16 *copper;
    u8 *chipPlane[3];
    u8 *shadow[3];
    u16 *sprite, *nullSprite;
    s8 *abuf[2][4];             /* [half][channel] chip */
    s16 L[AUDIO_BLOCK], R[AUDIO_BLOCK];
    volatile int nextHalf, audioOn;
    Interrupt vb, inp;
    void *inPort, *inReq;
    int inputOn, vbOn, displayOn;
    volatile u32 vblanks;
} Plat;

int run_on_stack(StackSwap *ss, int (*fn)(void *), void *arg); /* start.s, returns d0 */
void vb_tramp(void);
void in_tramp(void);

/* ------------------------------------------------------------------ memory */
void *mem_alloc(App *a, u32 size, int chip)
{
    Plat *p = (Plat *)a->plat;
    return AllocVec(p->sys, size ? size : 4, (chip ? MEMF_CHIP : MEMF_ANY) | MEMF_CLEAR);
}

void mem_free(App *a, void *ptr)
{
    Plat *p = (Plat *)a->plat;
    if (ptr) FreeVec(p->sys, ptr);
}

void audio_lock(App *a) { Disable(((Plat *)a->plat)->sys); }
void audio_unlock(App *a) { Enable(((Plat *)a->plat)->sys); }

/* ------------------------------------------------------------------ files */
void *f_open(App *a, const char *name, int write)
{
    Plat *p = (Plat *)a->plat;
    return (void *)DOpen(p->dos, name, write ? MODE_NEWFILE : MODE_OLDFILE);
}
s32 f_read(App *a, void *fh, void *buf, s32 len) { return DRead(((Plat *)a->plat)->dos, fh, buf, len); }
s32 f_write(App *a, void *fh, const void *buf, s32 len) { return DWrite(((Plat *)a->plat)->dos, fh, buf, len); }
s32 f_size(App *a, void *fh)
{
    void *db = ((Plat *)a->plat)->dos;
    DSeek(db, fh, 0, OFFSET_END);
    return DSeek(db, fh, 0, OFFSET_BEGINNING);
}
void f_close(App *a, void *fh) { DClose(((Plat *)a->plat)->dos, fh); }

static int lc_cmp(const char *x, const char *y)
{
    for (;; x++, y++) {
        int c1 = (u8)*x, c2 = (u8)*y;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2 || !c1) return c1 - c2;
    }
}

int dir_list(App *a, const char *path, DirEntry *out, int max)
{
    Plat *p = (Plat *)a->plat;
    u32 lock;
    u8 *fib;
    int n = 0, i, j;
    lock = DLock(p->dos, path, ACCESS_READ);
    if (!lock) return -1;
    fib = (u8 *)AllocDosObject(p->dos, DOS_FIB, 0);
    if (!fib) { DUnLock(p->dos, lock); return -1; }
    if (DExamine(p->dos, lock, fib) && *(s32 *)(fib + FIB_DIRENTRYTYPE) > 0) {
        while (n < max && DExNext(p->dos, lock, fib)) {
            DirEntry *d = &out[n];
            str_ncpy(d->name, (const char *)(fib + FIB_FILENAME), sizeof(d->name));
            d->isDir = *(s32 *)(fib + FIB_DIRENTRYTYPE) > 0;
            d->size = *(s32 *)(fib + FIB_SIZE);
            if (d->name[0] == 0) continue;
            n++;
        }
    }
    FreeDosObject(p->dos, DOS_FIB, fib);
    DUnLock(p->dos, lock);
    /* directories first, then by name (insertion sort) */
    for (i = 1; i < n; i++) {
        DirEntry t = out[i];
        j = i - 1;
        while (j >= 0 && ((out[j].isDir < t.isDir) || (out[j].isDir == t.isDir && lc_cmp(out[j].name, t.name) > 0))) {
            out[j + 1] = out[j]; j--;
        }
        out[j + 1] = t;
    }
    return n;
}

/* ------------------------------------------------------------------ input */
static void push_event(App *a, int type, int down, int code, int qual)
{
    int h = a->evHead, nh = (h + 1) % EVQ;
    Event *e;
    if (nh == a->evTail) return;      /* full: drop */
    e = &a->ev[h];
    e->type = (u8)type; e->down = (u8)down; e->code = (u16)code; e->qual = (u16)qual;
    e->x = (s16)a->mouseX; e->y = (s16)a->mouseY;
    a->evHead = nh;
}

/* called by input.device (via in_tramp): a0 = event list, a1 = Plat */
u32 input_handler(u8 *ev, Plat *p)
{
    App *a = p->app;
    u8 *ie;
    for (ie = ev; ie; ie = *(u8 **)ie) {
        u8 cls = ie[4];
        u16 code = *(u16 *)(ie + 6), qual = *(u16 *)(ie + 8);
        if (cls == 1) {                       /* RAWKEY */
            int k = code & 0x7F;
            if (k < 0x60 || k > 0x67) push_event(a, EV_KEY, !(code & 0x80), k, qual);
            ie[4] = 0;                        /* swallow */
        } else if (cls == 2) {                /* RAWMOUSE */
            s16 dx = *(s16 *)(ie + 10), dy = *(s16 *)(ie + 12);
            if (dx || dy) {
                int x = a->mouseX + dx * 2, y = a->mouseY + dy;
                if (x < 0) x = 0;
                if (x > SCREEN_W - 1) x = SCREEN_W - 1;
                if (y < 0) y = 0;
                if (y > SCREEN_H - 1) y = SCREEN_H - 1;
                a->mouseX = x; a->mouseY = y;
                push_event(a, EV_MOVE, 0, 0, qual);
            }
            if ((code & 0x7F) == 0x68) { a->mouseL = !(code & 0x80); push_event(a, EV_MOUSE, a->mouseL, 0, qual); }
            else if ((code & 0x7F) == 0x69) { a->mouseR = !(code & 0x80); push_event(a, EV_MOUSE, a->mouseR, 1, qual); }
            ie[4] = 0;
        }
    }
    return (u32)ev;
}

int plat_get_event(App *a, Event *e)
{
    int t = a->evTail;
    if (t == a->evHead) return 0;
    *e = a->ev[t];
    a->evTail = (t + 1) % EVQ;
    return 1;
}

/* ------------------------------------------------------------------ audio */
static void audio_fill(Plat *p, int half)
{
    App *a = p->app;
    s8 *h0 = p->abuf[half][0], *h1 = p->abuf[half][1], *h2 = p->abuf[half][2], *h3 = p->abuf[half][3];
    int i;
    player_render(a, p->L, p->R, AUDIO_BLOCK);
    /* 14-bit trick: ch0/ch1 carry the top 8 bits at volume 64,
       ch3/ch2 the next 6 bits at volume 1. Left = 0+3, right = 1+2. */
    for (i = 0; i < AUDIO_BLOCK; i++) {
        s32 l = p->L[i], r = p->R[i];
        h0[i] = (s8)(l >> 8);
        h3[i] = (s8)((l >> 2) & 0x3F);
        h1[i] = (s8)(r >> 8);
        h2[i] = (s8)((r >> 2) & 0x3F);
    }
}

static void audio_set_lc(Plat *p, int half)
{
    int c;
    for (c = 0; c < 4; c++) *(volatile u32 *)((u8 *)CUSTOM + AUD(c, AUD_LC)) = (u32)p->abuf[half][c];
}

static void audio_start(Plat *p)
{
    int c;
    R(DMACON) = 0x000F;
    for (c = 0; c < 4; c++) {
        R(AUD(c, AUD_LEN)) = AUDIO_BLOCK / 2;
        R(AUD(c, AUD_PER)) = 124;
        R(AUD(c, AUD_VOL)) = (c == 0 || c == 1) ? 64 : 1;
    }
    audio_set_lc(p, 0);
    p->nextHalf = 1;
    R(INTREQ) = 0x0780;
    p->audioOn = 1;
    R(DMACON) = 0x800F;
}

static void audio_stop(Plat *p)
{
    int c;
    p->audioOn = 0;
    R(DMACON) = 0x000F;
    for (c = 0; c < 4; c++) R(AUD(c, AUD_VOL)) = 0;
    R(INTREQ) = 0x0780;
}

/* ------------------------------------------------------------------ vblank */
static void sprite_pos(Plat *p, int x, int y)
{
    int vs = 0x2C + y, hs = 0x80 + x / 2, ve = vs + 11;
    p->sprite[0] = (u16)(((vs & 0xFF) << 8) | ((hs >> 1) & 0xFF));
    p->sprite[1] = (u16)(((ve & 0xFF) << 8) | (((vs >> 8) & 1) << 2) | (((ve >> 8) & 1) << 1) | (hs & 1));
}

/* called by exec (via vb_tramp) every vertical blank, a1 = Plat */
void vb_handler(Plat *p)
{
    p->vblanks++;
    if (p->audioOn && (R(INTREQR) & 0x0080)) {
        int h = p->nextHalf;
        R(INTREQ) = 0x0780;
        audio_set_lc(p, h);
        audio_fill(p, h);
        p->nextHalf = h ^ 1;
    }
    sprite_pos(p, p->app->mouseX, p->app->mouseY);
    Signal(p->sys, p->task, p->frameSig);
}

void plat_wait_frame(App *a)
{
    Plat *p = (Plat *)a->plat;
    Wait(p->sys, p->frameSig);
}

/* ------------------------------------------------------------------ display */
static const char *const arrow[11] = {
    "X               ",
    "XX              ",
    "XwX             ",
    "XwwX            ",
    "XwwwX           ",
    "XwwwwX          ",
    "XwwwwwX         ",
    "XwwwXXXX        ",
    "XwXwX           ",
    "XX XwX          ",
    "X   XX          ",
};

static void build_sprite(Plat *p)
{
    int r, c;
    for (r = 0; r < 11; r++) {
        u16 pa = 0, pb = 0;
        for (c = 0; c < 16; c++) {
            if (arrow[r][c] == 'X') pa |= (u16)(0x8000 >> c);
            if (arrow[r][c] == 'w') pb |= (u16)(0x8000 >> c);
        }
        p->sprite[2 + r * 2] = pa;
        p->sprite[3 + r * 2] = pb;
    }
    p->sprite[24] = 0; p->sprite[25] = 0;
    p->nullSprite[0] = 0; p->nullSprite[1] = 0;
    sprite_pos(p, 320, 128);
}

static const u16 colours[8] = { 0xFFF, 0x000, 0xDDD, 0x000, 0xAAA, 0x000, 0x999, 0x000 };

static void build_copper(Plat *p)
{
    u16 *c = p->copper;
    int i;
#define MOVE(reg, val) do { *c++ = (u16)(reg); *c++ = (u16)(val); } while (0)
    MOVE(0x1FC, 0);          /* FMODE */
    MOVE(0x100, 0xB200);     /* BPLCON0: hires, 3 planes, colour */
    MOVE(0x102, 0);
    MOVE(0x104, 0x0024);     /* sprites in front */
    MOVE(0x106, 0x0C00);
    MOVE(0x10C, 0x0011);
    MOVE(0x08E, 0x2C81); MOVE(0x090, 0x2CC1);
    MOVE(0x092, 0x003C); MOVE(0x094, 0x00D4);
    MOVE(0x108, 0); MOVE(0x10A, 0);
    for (i = 0; i < 3; i++) {
        u32 a = (u32)p->chipPlane[i];
        MOVE(0x0E0 + i * 4, a >> 16); MOVE(0x0E2 + i * 4, a & 0xFFFF);
    }
    for (i = 0; i < 8; i++) {
        u32 a = (u32)(i == 0 ? p->sprite : p->nullSprite);
        MOVE(0x120 + i * 4, a >> 16); MOVE(0x122 + i * 4, a & 0xFFFF);
    }
    for (i = 0; i < 8; i++) MOVE(0x180 + i * 2, colours[i]);
    MOVE(0x1A2, 0x000); MOVE(0x1A4, 0xFFF); MOVE(0x1A6, 0xF80);
    *c++ = 0xFFFF; *c++ = 0xFFFE;
#undef MOVE
}

static void present(Plat *p)
{
    App *a = p->app;
    int pl, y;
    for (pl = 0; pl < 3; pl++) {
        const u32 *src = (const u32 *)a->plane[pl];
        u32 *sh = (u32 *)p->shadow[pl];
        u32 *dst = (u32 *)p->chipPlane[pl];
        for (y = 0; y < SCREEN_H; y++) {
            int k, o = y * (ROWBYTES / 4), diff = 0;
            for (k = 0; k < ROWBYTES / 4; k++) if (src[o + k] != sh[o + k]) { diff = 1; break; }
            if (!diff) continue;
            for (k = 0; k < ROWBYTES / 4; k++) { sh[o + k] = src[o + k]; dst[o + k] = src[o + k]; }
        }
    }
}

/* ------------------------------------------------------------------ sampler */
int sampler_peek(App *a, int chan)
{
    (void)a;
    *CIAA_DDRB = 0;
    *CIAB_DDRA |= 0x04;
    if (chan) *CIAB_PRA |= 0x04; else *CIAB_PRA &= (u8)~0x04;
    return (s32)(s8)(*CIAA_PRB ^ 0x80) * 256;
}

static u32 beam(void)
{
    u32 v = *(volatile u32 *)((u8 *)CUSTOM + VPOSR);
    return ((v >> 8) & 0x1FF) * 227 + (v & 0xFF);
}

u32 sampler_record(App *a, s16 *buf, u32 frames, u32 rate, int chan)
{
    Plat *p = (Plat *)a->plat;
    u32 per8 = (3546895u * 256u) / rate, acc = 0, last, i = 0;
    int c;
    if (!rate) return 0;
    *CIAA_DDRB = 0;
    *CIAB_DDRA |= 0x04;
    if (chan) *CIAB_PRA |= 0x04; else *CIAB_PRA &= (u8)~0x04;
    for (c = 0; c < 4; c++) R(AUD(c, AUD_VOL)) = 0;   /* silence Paula while frozen */
    Disable(p->sys);
    last = beam();
    while (i < frames) {
        u32 now = beam();
        s32 d = (s32)now - (s32)last;
        if (d < 0) d += 313 * 227;
        last = now;
        acc += (u32)d << 8;
        while (acc >= per8 && i < frames) {
            acc -= per8;
            buf[i++] = (s16)((s32)(s8)(*CIAA_PRB ^ 0x80) * 256);
        }
        if ((i & 255) == 0 && !(*CIAA_PRA & 0x40) && i > 0) break;   /* left mouse */
    }
    Enable(p->sys);
    for (c = 0; c < 4; c++) R(AUD(c, AUD_VOL)) = (c == 0 || c == 1) ? 64 : 1;
    return i;
}

/* ------------------------------------------------------------------ main */
static int plat_setup(Plat *p)
{
    App *a = p->app;
    int i, c;
    for (i = 0; i < 3; i++) {
        a->plane[i] = (u8 *)AllocVec(p->sys, PLANE_BYTES, MEMF_ANY | MEMF_CLEAR);
        p->shadow[i] = (u8 *)AllocVec(p->sys, PLANE_BYTES, MEMF_ANY | MEMF_CLEAR);
        p->chipPlane[i] = (u8 *)AllocVec(p->sys, PLANE_BYTES, MEMF_CHIP | MEMF_CLEAR);
        if (!a->plane[i] || !p->shadow[i] || !p->chipPlane[i]) return 0;
    }
    p->copper = (u16 *)AllocVec(p->sys, 512, MEMF_CHIP | MEMF_CLEAR);
    p->sprite = (u16 *)AllocVec(p->sys, 64, MEMF_CHIP | MEMF_CLEAR);
    p->nullSprite = (u16 *)AllocVec(p->sys, 8, MEMF_CHIP | MEMF_CLEAR);
    if (!p->copper || !p->sprite || !p->nullSprite) return 0;
    for (i = 0; i < 2; i++)
        for (c = 0; c < 4; c++) {
            p->abuf[i][c] = (s8 *)AllocVec(p->sys, AUDIO_BLOCK, MEMF_CHIP | MEMF_CLEAR);
            if (!p->abuf[i][c]) return 0;
        }
    p->task = FindTask(p->sys, 0);
    {
        s32 sig = AllocSignal(p->sys, -1);
        if (sig < 0) return 0;
        p->frameSig = 1u << sig;
    }
    a->mouseX = 320; a->mouseY = 128;
    return 1;
}

static void plat_teardown(Plat *p)
{
    App *a = p->app;
    int i, c;
    for (i = 0; i < 3; i++) {
        if (a->plane[i]) FreeVec(p->sys, a->plane[i]);
        if (p->shadow[i]) FreeVec(p->sys, p->shadow[i]);
        if (p->chipPlane[i]) FreeVec(p->sys, p->chipPlane[i]);
    }
    if (p->copper) FreeVec(p->sys, p->copper);
    if (p->sprite) FreeVec(p->sys, p->sprite);
    if (p->nullSprite) FreeVec(p->sys, p->nullSprite);
    for (i = 0; i < 2; i++) for (c = 0; c < 4; c++) if (p->abuf[i][c]) FreeVec(p->sys, p->abuf[i][c]);
    if (p->frameSig) {
        int s = 0;
        while (!((p->frameSig >> s) & 1)) s++;
        FreeSignal(p->sys, s);
    }
}

static int input_on(Plat *p)
{
    p->inPort = CreateMsgPort(p->sys);
    if (!p->inPort) return 0;
    p->inReq = CreateIORequest(p->sys, p->inPort, 48);
    if (!p->inReq) return 0;
    if (OpenDevice(p->sys, "input.device", 0, p->inReq, 0) != 0) return 0;
    p->inp.type = 2; p->inp.pri = 100;
    p->inp.name = "PaulasRevenge input";
    p->inp.data = p;
    p->inp.code = (void *)in_tramp;
    *(u16 *)((u8 *)p->inReq + IO_COMMAND) = IND_ADDHANDLER;
    *(void **)((u8 *)p->inReq + IO_DATA) = &p->inp;
    DoIO(p->sys, p->inReq);
    p->inputOn = 1;
    return 1;
}

static void input_off(Plat *p)
{
    if (p->inputOn) {
        *(u16 *)((u8 *)p->inReq + IO_COMMAND) = IND_REMHANDLER;
        *(void **)((u8 *)p->inReq + IO_DATA) = &p->inp;
        DoIO(p->sys, p->inReq);
        CloseDevice(p->sys, p->inReq);
        p->inputOn = 0;
    }
    if (p->inReq) { DeleteIORequest(p->sys, p->inReq); p->inReq = 0; }
    if (p->inPort) { DeleteMsgPort(p->sys, p->inPort); p->inPort = 0; }
}

static void display_on(Plat *p)
{
    build_sprite(p);
    build_copper(p);
    p->oldView = *(void **)((u8 *)p->gfx + GFX_ACTIVIEW);
    LoadView(p->gfx, 0);
    WaitTOF(p->gfx); WaitTOF(p->gfx);
    *(volatile u32 *)((u8 *)CUSTOM + COP1LC) = (u32)p->copper;
    R(COPJMP1) = 0;
    R(DMACON) = 0x83A0;     /* DMAEN | BPLEN | COPEN | SPREN */
    p->displayOn = 1;
}

static void display_off(Plat *p)
{
    if (!p->displayOn) return;
    *(volatile u32 *)((u8 *)CUSTOM + COP1LC) = *(u32 *)((u8 *)p->gfx + GFX_COPINIT);
    R(COPJMP1) = 0;
    LoadView(p->gfx, p->oldView);
    WaitTOF(p->gfx); WaitTOF(p->gfx);
    RethinkDisplay(p->intui);
    p->displayOn = 0;
}

static int amiga_main(void *arg)
{
    Plat *p = (Plat *)arg;
    App *a;
    u8 *proc;
    a = (App *)AllocVec(p->sys, sizeof(App), MEMF_ANY | MEMF_CLEAR);
    if (!a) return 20;
    p->app = a;
    a->plat = p;
    if (!plat_setup(p) || !ui_init(a)) { plat_teardown(p); FreeVec(p->sys, a); return 20; }

    proc = (u8 *)p->task;
    p->oldWinPtr = *(void **)(proc + PR_WINDOWPTR);
    *(void **)(proc + PR_WINDOWPTR) = (void *)-1;      /* no "insert disk" requesters */

    if (input_on(p)) {
        display_on(p);
        audio_start(p);
        p->vb.type = 2; p->vb.pri = 0; p->vb.name = "PaulasRevenge vblank";
        p->vb.data = p; p->vb.code = (void *)vb_tramp;
        AddIntServer(p->sys, 5, &p->vb);
        p->vbOn = 1;

        while (a->ui.running) {
            plat_wait_frame(a);
            ui_frame(a);
            present(p);
            CacheClearU(p->sys);
        }

        RemIntServer(p->sys, 5, &p->vb);
        audio_stop(p);
        display_off(p);
    }
    input_off(p);
    *(void **)(proc + PR_WINDOWPTR) = p->oldWinPtr;
    song_free(a, a->song);
    if (a->ui.clip) FreeVec(p->sys, a->ui.clip);
    if (a->ui.undo.data) FreeVec(p->sys, a->ui.undo.data);
    if (a->song) FreeVec(p->sys, a->song);
    plat_teardown(p);
    FreeVec(p->sys, a);
    return 0;
}

/* entry from _start, on the caller's (small) stack */
int amiga_entry(void)
{
    void *sys = SYSBASE;
    Plat *p;
    u8 *proc, *stack;
    void *wbmsg = 0;
    int rc = 20;
    StackSwap ss;

    proc = (u8 *)FindTask(sys, 0);
    if (*(u32 *)(proc + PR_CLI) == 0) {          /* started from Workbench */
        WaitPort(sys, proc + PR_MSGPORT);
        wbmsg = GetMsg(sys, proc + PR_MSGPORT);
    }
    if (!(*(u16 *)((u8 *)sys + EXEC_ATTNFLAGS) & 0x02)) goto out;   /* needs 68020+ */
    p = (Plat *)AllocVec(sys, sizeof(Plat), MEMF_ANY | MEMF_CLEAR);
    if (!p) goto out;
    p->sys = sys;
    p->dos = OpenLibrary(sys, "dos.library", 39);
    p->gfx = OpenLibrary(sys, "graphics.library", 39);
    p->intui = OpenLibrary(sys, "intuition.library", 39);
    stack = (u8 *)AllocVec(sys, STACK_SIZE, MEMF_ANY);
    if (p->dos && p->gfx && p->intui && stack) {
        ss.lower = stack;
        ss.upper = (u32)(stack + STACK_SIZE);
        ss.pointer = stack + STACK_SIZE;
        rc = run_on_stack(&ss, amiga_main, p);
    }
    if (stack) FreeVec(sys, stack);
    if (p->intui) CloseLibrary(sys, p->intui);
    if (p->gfx) CloseLibrary(sys, p->gfx);
    if (p->dos) CloseLibrary(sys, p->dos);
    FreeVec(sys, p);
out:
    if (wbmsg) { Forbid(sys); ReplyMsg(sys, wbmsg); }
    return rc;
}
