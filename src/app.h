#ifndef APP_H
#define APP_H
#include "types.h"
#include "song.h"

/* IMPORTANT (68k backend rule): this program never writes to global or
   static variables. Every piece of mutable state lives in App, which is
   heap-allocated at startup and passed to every function. */

#define OUT_RATE    28604   /* Paula period 124 on PAL */
#define AUDIO_BLOCK 1024    /* frames per Paula half-buffer */

/* ---------- player / mixer ---------- */
typedef struct {
    const Instr *ins;
    const s16 *data;
    u32 pos, frac;          /* frac is 16-bit fraction */
    u32 step;               /* 16.16 */
    u32 end;                /* play until (exclusive) */
    u32 loopStart, loopEnd; /* loopEnd 0 = no loop */
    u8  active, instrNum, note, vol;
    u8  pan, cmd, param, vibPos;
    u8  vibSpeed, vibDepth, lastVolSlide, retrig;
    u8  delay, cutTick, pad0, pad1;
    u32 startPos;
    s32 arpOffset;
    s32 pitch;              /* 1/64 semitone units, note*64 + fine */
    s32 targetPitch;
    s32 vibOffset;
    u16 portaSpeed, slideSpeed;
    s32 gainL, gainR;       /* 0..1024 */
    Cell pending;           /* note-delay cell */
} Chan;

#define PLAY_STOP  0
#define PLAY_SONG  1
#define PLAY_BLOCK 2

typedef struct {
    volatile int mode;
    int orderPos, secSlot, block, line, tick;
    int speed, bpm;
    u32 tickLen, tickLeft;  /* in output frames */
    int breakTo, jumpTo;    /* -1 none */
    int stopReq;
    Chan ch[MAX_TRACKS];
    Chan preview;
    s32 mixL[AUDIO_BLOCK], mixR[AUDIO_BLOCK];
    volatile int curLine, curBlock, curOrder; /* for display */
} Player;

/* ---------- input ---------- */
#define EV_KEY   1
#define EV_MOUSE 2   /* button change */
#define EV_MOVE  3

typedef struct { u8 type, down; u16 code; u16 qual; s16 x, y; } Event;
#define EVQ 128

/* qualifiers (Amiga input event values) */
#define Q_LSHIFT 0x01
#define Q_RSHIFT 0x02
#define Q_SHIFT  0x03
#define Q_CTRL   0x08
#define Q_LALT   0x10
#define Q_RALT   0x20
#define Q_ALT    0x30
#define Q_LAMIGA 0x40
#define Q_RAMIGA 0x80
#define Q_REPEAT 0x200

/* ---------- file browser ---------- */
#define MAX_DIR 256
typedef struct { char name[32]; s32 size; u8 isDir; } DirEntry;

/* ---------- UI ---------- */
#define SCR_BLOCK  0
#define SCR_ARRANGE 1
#define SCR_SAMPLE 2
#define SCR_INSTR  3
#define SCR_DISK   4
#define SCR_HELP   5

#define COLS 80
#define ROWS 25
#define FONT_H 10
#define SCREEN_W 640
#define SCREEN_H 256
#define ROWBYTES 80

typedef struct {
    int screen, prevScreen;
    /* block editor */
    int block, line, track, col, trackOfs;
    int octave, instr, edit, step;
    int selActive, selT0, selT1, selL0, selL1;
    /* clipboard */
    Cell *clip; int clipLines, clipTracks;
    /* arrange */
    int ordCur, secSlotCur, secListCur, arrFocus;
    /* sample editor */
    u32 viewStart, viewLen, rangeA, rangeB;
    int dragging;
    int recRate, recSeconds, recChan, monitor, recArm;
    Instr undo; int undoSlot, undoValid;
    /* instruments */
    int insField;
    /* disk */
    char path[256];
    DirEntry dir[MAX_DIR];
    int dirCount, dirCur, dirTop, diskMode; /* 0 load, 1 load sample, 2 save song */
    /* prompt */
    int promptActive, promptKind, promptLen, promptFresh;
    char promptLabel[40];
    char promptBuf[64];
    /* status */
    char status[81];
    int statusTimer;
    int dirty, quitAsk, running;
    int help_page;
} Ui;

typedef struct App {
    void *plat;                 /* platform private state */
    u8 *plane[3];               /* 640x256 bitplanes, ROWBYTES per row */
    volatile int mouseX, mouseY, mouseL, mouseR;
    volatile int evHead, evTail;
    Event ev[EVQ];
    Song *song;
    Player pl;
    Ui ui;
    u32 frame;
    char tmp[256];
} App;

/* ---------- platform layer (plat_amiga.c / plat_host.c) ---------- */
void *mem_alloc(App *a, u32 size, int chip);  /* zeroed */
void  mem_free(App *a, void *p);
void  audio_lock(App *a);
void  audio_unlock(App *a);
void  plat_wait_frame(App *a);
int   plat_get_event(App *a, Event *e);
void *f_open(App *a, const char *name, int write);
s32   f_read(App *a, void *fh, void *buf, s32 len);
s32   f_write(App *a, void *fh, const void *buf, s32 len);
s32   f_size(App *a, void *fh);
void  f_close(App *a, void *fh);
int   dir_list(App *a, const char *path, DirEntry *out, int max);
int   sampler_peek(App *a, int chan);                 /* -32768..32767 */
u32   sampler_record(App *a, s16 *buf, u32 frames, u32 rate, int chan);

/* ---------- player.c ---------- */
void player_init(App *a);
void player_play(App *a, int mode, int fromCursor);
void player_stop(App *a);
void player_forget_instr(App *a, const Instr *in); /* call under audio_lock */
void player_render(App *a, s16 *outL, s16 *outR, int frames);
void player_preview(App *a, int instr, int note, u32 start, u32 end);
void player_preview_stop(App *a);
void player_set_tempo(App *a, int bpm);
u32  pitch_to_step(s32 pitch, u32 baseRate);

/* ---------- ui.c ---------- */
int  ui_init(App *a);
void ui_frame(App *a);    /* process events + redraw */
void ui_status(App *a, const char *msg);

/* ---------- songio.c ---------- */
int song_save(App *a, const char *path);
int song_load(App *a, const char *path);
int sample_load(App *a, const char *path, int slot);
int sample_save_iff(App *a, const char *path, int slot);

/* ---------- sampletools.c ---------- */
void st_normalise(App *a, Instr *in, u32 s, u32 e);
void st_reverse(App *a, Instr *in, u32 s, u32 e);
void st_fade(App *a, Instr *in, u32 s, u32 e, int out);
void st_silence(App *a, Instr *in, u32 s, u32 e);
void st_gain(App *a, Instr *in, u32 s, u32 e, int percent);
int  st_crop(App *a, Instr *in, u32 s, u32 e);
int  st_cut(App *a, Instr *in, u32 s, u32 e);
int  st_resample(App *a, Instr *in, int semitones);
void st_loop_xfade(App *a, Instr *in, u32 xlen);
int  st_chop_equal(App *a, Instr *in, int n);
int  st_chop_transient(App *a, Instr *in, int sens);
int  st_slices_to_instruments(App *a, int src);

/* ---------- gfx (draw.c) ---------- */
void gfx_clear(App *a);
void gfx_text(App *a, int col, int row, const char *s, int inv);
void gfx_textn(App *a, int col, int row, const char *s, int n, int inv);
void gfx_fill_row(App *a, int row, int c0, int c1, int plane, int on);
void gfx_clear_rows(App *a, int r0, int r1);
void gfx_hline(App *a, int x0, int x1, int y, int plane);
void gfx_vline(App *a, int x, int y0, int y1, int plane);
void gfx_rect_fill(App *a, int x0, int y0, int x1, int y1, int plane, int on);
void gfx_pset(App *a, int x, int y, int plane);

#endif
