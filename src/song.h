#ifndef SONG_H
#define SONG_H
#include "types.h"

#define MAX_TRACKS    16
#define MAX_BLOCKS    128
#define MAX_LINES     256
#define MAX_INSTR     64      /* slot 0 unused; 01..3F like OctaMED */
#define MAX_SECTIONS  64
#define MAX_SEC_BLOCKS 64
#define MAX_ORDER     256
#define MAX_SLICES    64

#define NOTE_NONE 0
#define NOTE_OFF  127         /* shown as "-|-" */
#define NOTE_MAX  96          /* 1..96 = C-1 .. B-8 */
#define NOTE_C3   25          /* plays sample at its base rate */

typedef struct {
    u8 note;    /* 0 none, 1..96, 127 off */
    u8 instr;   /* 0 none, 1..63 */
    u8 cmd;     /* OctaMED-style command number */
    u8 param;
} Cell;

typedef struct {
    char name[16];
    u16 lines;          /* 1..256 */
    s16 parent;         /* -1 = original block, else block it varies */
    u16 own;            /* variations: bit per track, 1 = own data, 0 = linked to parent */
    u16 used;           /* 1 if block slot exists */
    Cell *data;         /* MAX_LINES * MAX_TRACKS cells, allocated */
} Block;

typedef struct {
    char name[16];
    u8 count;
    u8 blocks[MAX_SEC_BLOCKS];
    u8 used;
} Section;

typedef struct {
    char name[24];
    s16 *data;          /* 16-bit signed mono sample data */
    u32 length;         /* frames */
    u32 loopStart;
    u32 loopLen;        /* 0 = no loop */
    u32 baseRate;       /* Hz at C-3 */
    u8  volume;         /* 0..64 */
    u8  pan;            /* 0 = left, 128 = centre, 255 = right */
    s8  finetune;       /* -8..7 eighths of a semitone */
    s8  transpose;      /* semitones */
    u8  nslices;        /* 0 = normal, else chop-to-keys */
    u8  sliceBase;      /* note number mapped to slice 0 */
    u16 pad;
    u32 slice[MAX_SLICES]; /* slice start frames, ascending */
} Instr;

typedef struct {
    char name[32];
    u16 bpm;            /* 32..255 */
    u8  speed;          /* ticks per line, 1..32 */
    u8  tracks;         /* tracks in use, 1..16 */
    u8  masterVol;      /* 0..64 */
    u8  interp;         /* 0 = Paula-style (none), 1 = linear */
    u16 orderLen;
    u8  order[MAX_ORDER];   /* section indices: the timeline */
    u8  trackPan[MAX_TRACKS];
    u8  trackMute[MAX_TRACKS];
    Block blocks[MAX_BLOCKS];
    Section sections[MAX_SECTIONS];
    Instr instr[MAX_INSTR];
} Song;

struct App;

/* song.c */
int  song_init(struct App *a, Song *s);           /* empty song with one block/section */
void song_free(struct App *a, Song *s);
int  block_new(struct App *a, Song *s, int lines);        /* returns index or -1 */
int  block_clone(struct App *a, Song *s, int src);        /* independent copy */
int  block_make_variation(struct App *a, Song *s, int src); /* linked variation */
void block_delete(struct App *a, Song *s, int b);
const Cell *block_cell(const Song *s, int b, int line, int track);
Cell *block_cell_edit(Song *s, int b, int line, int track); /* breaks link for that track */
int  block_track_linked(const Song *s, int b, int track);
void block_relink_track(Song *s, int b, int track);
void block_set_lines(Song *s, int b, int lines);
int  section_new(Song *s, const char *name);
int  section_clone(struct App *a, Song *s, int src, int asVariations);
void section_delete(Song *s, int sec);
int  song_total_lines(const Song *s);
int  section_lines(const Song *s, int sec);
void instr_clear(struct App *a, Instr *in);
void instr_set_sample(struct App *a, Instr *in, s16 *data, u32 len, u32 rate);

#endif
