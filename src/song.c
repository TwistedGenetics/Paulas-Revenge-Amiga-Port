/* Song model: blocks, linked variations, sections, instruments. */
#include "app.h"

#define CELLS (MAX_LINES * MAX_TRACKS)

static void block_init_fields(Block *b, int lines)
{
    memset(b->name, 0, sizeof(b->name));
    b->lines = (u16)lines;
    b->parent = -1;
    b->own = 0xFFFF;
    b->used = 1;
}

int song_init(App *a, Song *s)
{
    int i;
    memset(s, 0, sizeof(Song));
    str_ncpy(s->name, "untitled", sizeof(s->name));
    s->bpm = 125; s->speed = 6; s->tracks = 8; s->masterVol = 40; s->interp = 0;
    for (i = 0; i < MAX_TRACKS; i++) if ((i & 3) == 1 || (i & 3) == 2) s->trackPan[i] = 192; else s->trackPan[i] = 64;
    for (i = 0; i < MAX_BLOCKS; i++) s->blocks[i].parent = -1;
    for (i = 1; i < MAX_INSTR; i++) { s->instr[i].volume = 64; s->instr[i].pan = 128; s->instr[i].baseRate = 16726; }
    if (block_new(a, s, 64) < 0) return 0;
    section_new(s, "Main");
    s->sections[0].count = 1; s->sections[0].blocks[0] = 0;
    s->orderLen = 1; s->order[0] = 0;
    return 1;
}

void song_free(App *a, Song *s)
{
    int i;
    for (i = 0; i < MAX_BLOCKS; i++) if (s->blocks[i].data) { mem_free(a, s->blocks[i].data); s->blocks[i].data = NULL; }
    for (i = 0; i < MAX_INSTR; i++) if (s->instr[i].data) { mem_free(a, s->instr[i].data); s->instr[i].data = NULL; }
}

static int free_block_slot(Song *s)
{
    int i;
    for (i = 0; i < MAX_BLOCKS; i++) if (!s->blocks[i].used) return i;
    return -1;
}

int block_new(App *a, Song *s, int lines)
{
    int i = free_block_slot(s);
    if (i < 0) return -1;
    if (!s->blocks[i].data) {
        s->blocks[i].data = (Cell *)mem_alloc(a, CELLS * sizeof(Cell), 0);
        if (!s->blocks[i].data) return -1;
    } else memset(s->blocks[i].data, 0, CELLS * sizeof(Cell));
    block_init_fields(&s->blocks[i], lines);
    return i;
}

static int root_of(const Song *s, int b)
{
    int guard = 0;
    while (b >= 0 && s->blocks[b].parent >= 0 && guard++ < MAX_BLOCKS) b = s->blocks[b].parent;
    return b;
}

/* Materialise every track so the block no longer depends on its parent. */
static void block_detach(Song *s, int b)
{
    Block *bl = &s->blocks[b];
    int t, l;
    if (bl->parent < 0) return;
    for (t = 0; t < MAX_TRACKS; t++) {
        if (bl->own & (1 << t)) continue;
        for (l = 0; l < MAX_LINES; l++)
            bl->data[l * MAX_TRACKS + t] = *block_cell(s, bl->parent, l, t);
    }
    bl->own = 0xFFFF;
    bl->parent = -1;
}

int block_clone(App *a, Song *s, int src)
{
    int n = block_new(a, s, s->blocks[src].lines);
    int t, l;
    if (n < 0) return -1;
    for (l = 0; l < MAX_LINES; l++)
        for (t = 0; t < MAX_TRACKS; t++)
            s->blocks[n].data[l * MAX_TRACKS + t] = *block_cell(s, src, l, t);
    memcpy(s->blocks[n].name, s->blocks[src].name, sizeof(s->blocks[n].name));
    return n;
}

int block_make_variation(App *a, Song *s, int src)
{
    int root = root_of(s, src);
    int n = block_new(a, s, s->blocks[src].lines);
    int t, l;
    if (n < 0) return -1;
    s->blocks[n].parent = (s16)root;
    s->blocks[n].own = 0;
    /* a variation of a variation keeps the source's own tracks as own data */
    if (root != src) {
        for (t = 0; t < MAX_TRACKS; t++) {
            if (!(s->blocks[src].own & (1 << t))) continue;
            for (l = 0; l < MAX_LINES; l++)
                s->blocks[n].data[l * MAX_TRACKS + t] = s->blocks[src].data[l * MAX_TRACKS + t];
            s->blocks[n].own |= (u16)(1 << t);
        }
    }
    memcpy(s->blocks[n].name, s->blocks[src].name, sizeof(s->blocks[n].name));
    return n;
}

void block_delete(App *a, Song *s, int b)
{
    int i, j;
    (void)a;
    if (!s->blocks[b].used) return;
    /* children of this block become independent copies */
    for (i = 0; i < MAX_BLOCKS; i++)
        if (s->blocks[i].used && s->blocks[i].parent == b) block_detach(s, i);
    s->blocks[b].used = 0;
    s->blocks[b].parent = -1;
    /* remove from sections */
    for (i = 0; i < MAX_SECTIONS; i++) {
        Section *sc = &s->sections[i];
        if (!sc->used) continue;
        for (j = 0; j < sc->count; ) {
            if (sc->blocks[j] == b) {
                memmove(&sc->blocks[j], &sc->blocks[j + 1], (unsigned long)(sc->count - j - 1));
                sc->count--;
            } else j++;
        }
    }
}

const Cell *block_cell(const Song *s, int b, int line, int track)
{
    int guard = 0;
    const Block *bl = &s->blocks[b];
    while (bl->parent >= 0 && !(bl->own & (1 << track)) && guard++ < 4) {
        bl = &s->blocks[bl->parent];
    }
    return &bl->data[line * MAX_TRACKS + track];
}

int block_track_linked(const Song *s, int b, int track)
{
    const Block *bl = &s->blocks[b];
    return bl->parent >= 0 && !(bl->own & (1 << track));
}

Cell *block_cell_edit(Song *s, int b, int line, int track)
{
    Block *bl = &s->blocks[b];
    if (bl->parent >= 0 && !(bl->own & (1 << track))) {
        int l;
        for (l = 0; l < MAX_LINES; l++)
            bl->data[l * MAX_TRACKS + track] = *block_cell(s, bl->parent, l, track);
        bl->own |= (u16)(1 << track);
    }
    return &bl->data[line * MAX_TRACKS + track];
}

void block_relink_track(Song *s, int b, int track)
{
    Block *bl = &s->blocks[b];
    if (bl->parent < 0) return;
    bl->own &= (u16)~(1 << track);
}

void block_set_lines(Song *s, int b, int lines)
{
    if (lines < 1) lines = 1;
    if (lines > MAX_LINES) lines = MAX_LINES;
    s->blocks[b].lines = (u16)lines;
}

int section_new(Song *s, const char *name)
{
    int i;
    for (i = 0; i < MAX_SECTIONS; i++) {
        if (!s->sections[i].used) {
            memset(&s->sections[i], 0, sizeof(Section));
            s->sections[i].used = 1;
            str_ncpy(s->sections[i].name, name, sizeof(s->sections[i].name));
            return i;
        }
    }
    return -1;
}

int section_clone(App *a, Song *s, int src, int asVariations)
{
    int n, j;
    Section *ss;
    if (src < 0 || !s->sections[src].used) return -1;
    n = section_new(s, s->sections[src].name);
    if (n < 0) return -1;
    ss = &s->sections[src];
    {
        /* append a marker so the copy is distinguishable */
        char *nm = s->sections[n].name;
        int len = (int)strlen(nm);
        if (len > 13) len = 13;
        nm[len] = asVariations ? '~' : '+';
        nm[len + 1] = 0;
    }
    for (j = 0; j < ss->count; j++) {
        int b = ss->blocks[j];
        int nb = asVariations ? block_make_variation(a, s, b) : (int)b;
        if (nb < 0) break;
        s->sections[n].blocks[s->sections[n].count++] = (u8)nb;
    }
    return n;
}

void section_delete(Song *s, int sec)
{
    int i;
    if (!s->sections[sec].used) return;
    s->sections[sec].used = 0;
    for (i = 0; i < s->orderLen; ) {
        if (s->order[i] == sec) {
            memmove(&s->order[i], &s->order[i + 1], (unsigned long)(s->orderLen - i - 1));
            s->orderLen--;
        } else i++;
    }
}

int section_lines(const Song *s, int sec)
{
    int j, n = 0;
    const Section *sc = &s->sections[sec];
    for (j = 0; j < sc->count; j++) n += s->blocks[sc->blocks[j]].lines;
    return n;
}

int song_total_lines(const Song *s)
{
    int i, n = 0;
    for (i = 0; i < s->orderLen; i++) n += section_lines(s, s->order[i]);
    return n;
}

void instr_clear(App *a, Instr *in)
{
    audio_lock(a);
    player_forget_instr(a, in);
    audio_unlock(a);
    if (in->data) mem_free(a, in->data);
    memset(in, 0, sizeof(Instr));
    in->volume = 64; in->pan = 128; in->baseRate = 16726;
}

void instr_set_sample(App *a, Instr *in, s16 *data, u32 len, u32 rate)
{
    s16 *old = in->data;
    audio_lock(a);
    player_forget_instr(a, in);
    in->data = data; in->length = len;
    in->loopStart = 0; in->loopLen = 0;
    in->nslices = 0;
    if (rate) in->baseRate = rate;
    audio_unlock(a);
    if (old) mem_free(a, old);
}
