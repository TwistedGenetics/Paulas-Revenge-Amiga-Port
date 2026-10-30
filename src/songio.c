/* Song save/load (own "PR12" format) and sample import/export.
   Sample import: IFF 8SVX, WAV (8/16/24-bit, mono/stereo), AIFF, raw 8-bit. */
#include "app.h"

/* ---------- buffered big-endian writer ---------- */
typedef struct { App *a; void *fh; u8 buf[4096]; int n; int err; } W;

static void w_flush(W *w)
{
    if (w->n && !w->err) { if (f_write(w->a, w->fh, w->buf, w->n) != w->n) w->err = 1; }
    w->n = 0;
}
static void w_bytes(W *w, const void *p, u32 len)
{
    const u8 *s = (const u8 *)p;
    while (len) {
        u32 k = sizeof(w->buf) - (u32)w->n;
        if (k > len) k = len;
        memcpy(w->buf + w->n, s, k);
        w->n += (int)k; s += k; len -= k;
        if (w->n == (int)sizeof(w->buf)) w_flush(w);
    }
}
static void w_u8(W *w, u32 v) { u8 b = (u8)v; w_bytes(w, &b, 1); }
static void w_u16(W *w, u32 v) { u8 b[2]; b[0] = (u8)(v >> 8); b[1] = (u8)v; w_bytes(w, b, 2); }
static void w_u32(W *w, u32 v) { u8 b[4]; b[0] = (u8)(v >> 24); b[1] = (u8)(v >> 16); b[2] = (u8)(v >> 8); b[3] = (u8)v; w_bytes(w, b, 4); }

/* ---------- memory reader ---------- */
typedef struct { const u8 *p; u32 len, pos; int err; } R;
static u32 r_u8(R *r) { if (r->pos + 1 > r->len) { r->err = 1; return 0; } return r->p[r->pos++]; }
static u32 r_u16(R *r) { u32 v; if (r->pos + 2 > r->len) { r->err = 1; return 0; } v = ((u32)r->p[r->pos] << 8) | r->p[r->pos + 1]; r->pos += 2; return v; }
static u32 r_u32(R *r) { u32 v = r_u16(r) << 16; return v | r_u16(r); }
static void r_bytes(R *r, void *d, u32 n) { if (r->pos + n > r->len) { r->err = 1; memset(d, 0, n); return; } memcpy(d, r->p + r->pos, n); r->pos += n; }

static u8 *read_whole(App *a, const char *path, u32 *len)
{
    void *fh = f_open(a, path, 0);
    s32 sz;
    u8 *buf;
    if (!fh) return NULL;
    sz = f_size(a, fh);
    if (sz <= 0) { f_close(a, fh); return NULL; }
    buf = (u8 *)mem_alloc(a, (u32)sz + 4, 0);
    if (!buf) { f_close(a, fh); return NULL; }
    if (f_read(a, fh, buf, sz) != sz) { f_close(a, fh); mem_free(a, buf); return NULL; }
    f_close(a, fh);
    *len = (u32)sz;
    return buf;
}

#define MAGIC 0x50523132  /* 'PR12' */

int song_save(App *a, const char *path)
{
    Song *s = a->song;
    W *w = (W *)mem_alloc(a, sizeof(W), 0);
    int i, n, l, t;
    if (!w) return 0;
    w->a = a;
    w->fh = f_open(a, path, 1);
    if (!w->fh) { mem_free(a, w); return 0; }
    w_u32(w, MAGIC); w_u32(w, 1);
    w_bytes(w, s->name, 32);
    w_u16(w, s->bpm); w_u8(w, s->speed); w_u8(w, s->tracks); w_u8(w, s->masterVol); w_u8(w, s->interp);
    w_u16(w, s->orderLen); w_bytes(w, s->order, MAX_ORDER);
    w_bytes(w, s->trackPan, MAX_TRACKS); w_bytes(w, s->trackMute, MAX_TRACKS);
    for (n = 0, i = 0; i < MAX_BLOCKS; i++) if (s->blocks[i].used) n++;
    w_u16(w, (u32)n);
    for (i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &s->blocks[i];
        if (!b->used) continue;
        w_u8(w, (u32)i); w_bytes(w, b->name, 16);
        w_u16(w, b->lines); w_u16(w, (u32)(u16)b->parent); w_u16(w, b->own);
        for (l = 0; l < b->lines; l++)
            for (t = 0; t < MAX_TRACKS; t++) w_bytes(w, &b->data[l * MAX_TRACKS + t], 4);
    }
    for (n = 0, i = 0; i < MAX_SECTIONS; i++) if (s->sections[i].used) n++;
    w_u16(w, (u32)n);
    for (i = 0; i < MAX_SECTIONS; i++) {
        Section *sc = &s->sections[i];
        if (!sc->used) continue;
        w_u8(w, (u32)i); w_bytes(w, sc->name, 16); w_u8(w, sc->count); w_bytes(w, sc->blocks, sc->count);
    }
    for (n = 0, i = 1; i < MAX_INSTR; i++) if (s->instr[i].data || s->instr[i].name[0]) n++;
    w_u16(w, (u32)n);
    for (i = 1; i < MAX_INSTR; i++) {
        Instr *in = &s->instr[i];
        u32 k;
        if (!in->data && !in->name[0]) continue;
        w_u8(w, (u32)i); w_bytes(w, in->name, 24);
        w_u32(w, in->data ? in->length : 0); w_u32(w, in->loopStart); w_u32(w, in->loopLen); w_u32(w, in->baseRate);
        w_u8(w, in->volume); w_u8(w, in->pan); w_u8(w, (u32)(u8)in->finetune); w_u8(w, (u32)(u8)in->transpose);
        w_u8(w, in->nslices); w_u8(w, in->sliceBase);
        for (k = 0; k < in->nslices; k++) w_u32(w, in->slice[k]);
        if (in->data) for (k = 0; k < in->length; k++) w_u16(w, (u32)(u16)in->data[k]);
    }
    w_flush(w);
    f_close(a, w->fh);
    n = !w->err;
    mem_free(a, w);
    if (n) a->ui.dirty = 0;
    return n;
}

int song_load(App *a, const char *path)
{
    Song *s = a->song;
    u32 len;
    u8 *buf = read_whole(a, path, &len);
    R r;
    int i, n, l, t;
    if (!buf) return 0;
    r.p = buf; r.len = len; r.pos = 0; r.err = 0;
    if (r_u32(&r) != MAGIC) { mem_free(a, buf); return -1; }
    r_u32(&r);
    player_stop(a);
    audio_lock(a);
    for (i = 0; i < MAX_TRACKS; i++) a->pl.ch[i].active = 0;
    a->pl.preview.active = 0;
    audio_unlock(a);
    song_free(a, s);
    memset(s, 0, sizeof(Song));
    for (i = 0; i < MAX_BLOCKS; i++) s->blocks[i].parent = -1;
    for (i = 1; i < MAX_INSTR; i++) { s->instr[i].volume = 64; s->instr[i].pan = 128; s->instr[i].baseRate = 16726; }
    r_bytes(&r, s->name, 32); s->name[31] = 0;
    s->bpm = (u16)r_u16(&r); s->speed = (u8)r_u8(&r); s->tracks = (u8)r_u8(&r); s->masterVol = (u8)r_u8(&r); s->interp = (u8)r_u8(&r);
    s->orderLen = (u16)r_u16(&r); r_bytes(&r, s->order, MAX_ORDER);
    r_bytes(&r, s->trackPan, MAX_TRACKS); r_bytes(&r, s->trackMute, MAX_TRACKS);
    if (s->tracks < 1 || s->tracks > MAX_TRACKS) s->tracks = 8;
    if (s->orderLen > MAX_ORDER) s->orderLen = 0;
    n = (int)r_u16(&r);
    while (n-- > 0 && !r.err) {
        int idx = (int)r_u8(&r);
        Block *b;
        if (idx >= MAX_BLOCKS) { r.err = 1; break; }
        b = &s->blocks[idx];
        b->data = (Cell *)mem_alloc(a, MAX_LINES * MAX_TRACKS * sizeof(Cell), 0);
        if (!b->data) { r.err = 1; break; }
        r_bytes(&r, b->name, 16); b->name[15] = 0;
        b->lines = (u16)r_u16(&r); b->parent = (s16)r_u16(&r); b->own = (u16)r_u16(&r);
        if (b->lines < 1 || b->lines > MAX_LINES) b->lines = 64;
        for (l = 0; l < b->lines; l++)
            for (t = 0; t < MAX_TRACKS; t++) r_bytes(&r, &b->data[l * MAX_TRACKS + t], 4);
        b->used = 1;
    }
    for (i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &s->blocks[i];
        if (b->used && (b->parent >= MAX_BLOCKS || (b->parent >= 0 && !s->blocks[b->parent].used))) { b->parent = -1; b->own = 0xFFFF; }
    }
    n = (int)r_u16(&r);
    while (n-- > 0 && !r.err) {
        int idx = (int)r_u8(&r);
        Section *sc;
        if (idx >= MAX_SECTIONS) { r.err = 1; break; }
        sc = &s->sections[idx];
        r_bytes(&r, sc->name, 16); sc->name[15] = 0;
        sc->count = (u8)r_u8(&r);
        if (sc->count > MAX_SEC_BLOCKS) { r.err = 1; break; }
        r_bytes(&r, sc->blocks, sc->count);
        for (i = 0; i < sc->count; i++) if (sc->blocks[i] >= MAX_BLOCKS || !s->blocks[sc->blocks[i]].used) sc->blocks[i] = 0;
        sc->used = 1;
    }
    for (i = 0; i < s->orderLen; i++) if (s->order[i] >= MAX_SECTIONS || !s->sections[s->order[i]].used) s->order[i] = 0;
    n = (int)r_u16(&r);
    while (n-- > 0 && !r.err) {
        int idx = (int)r_u8(&r);
        Instr *in;
        u32 k, slen;
        if (idx < 1 || idx >= MAX_INSTR) { r.err = 1; break; }
        in = &s->instr[idx];
        r_bytes(&r, in->name, 24); in->name[23] = 0;
        slen = r_u32(&r); in->loopStart = r_u32(&r); in->loopLen = r_u32(&r); in->baseRate = r_u32(&r);
        in->volume = (u8)r_u8(&r); in->pan = (u8)r_u8(&r); in->finetune = (s8)r_u8(&r); in->transpose = (s8)r_u8(&r);
        in->nslices = (u8)r_u8(&r); in->sliceBase = (u8)r_u8(&r);
        if (in->nslices > MAX_SLICES) { r.err = 1; break; }
        for (k = 0; k < in->nslices; k++) in->slice[k] = r_u32(&r);
        if (slen) {
            if (r.pos + slen * 2 > r.len) { r.err = 1; break; }
            in->data = (s16 *)mem_alloc(a, slen * 2 + 4, 0);
            if (!in->data) { r.err = 1; break; }
            for (k = 0; k < slen; k++) in->data[k] = (s16)r_u16(&r);
            in->length = slen;
        }
        if (in->loopStart + in->loopLen > in->length) { in->loopStart = 0; in->loopLen = 0; }
        for (k = 0; k < in->nslices; k++) if (in->slice[k] >= in->length) { in->nslices = (u8)k; break; }
    }
    mem_free(a, buf);
    /* guarantee at least one block and one section */
    for (i = 0; i < MAX_BLOCKS && !s->blocks[i].used; i++) ;
    if (i == MAX_BLOCKS) block_new(a, s, 64);
    for (i = 0; i < MAX_SECTIONS && !s->sections[i].used; i++) ;
    if (i == MAX_SECTIONS) { section_new(s, "Main"); }
    if (s->bpm < 32) s->bpm = 125;
    if (s->speed < 1 || s->speed > 32) s->speed = 6;
    a->ui.dirty = 0;
    return r.err ? -2 : 1;
}

/* ---------- sample import ---------- */
static u32 le16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 le32(const u8 *p) { return le16(p) | (le16(p + 2) << 16); }
static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return (be16(p) << 16) | be16(p + 2); }

static void base_name(const char *path, char *out, int n)
{
    const char *b = path, *p;
    int i;
    for (p = path; *p; p++) if (*p == '/' || *p == ':') b = p + 1;
    str_ncpy(out, b, n);
    for (i = (int)strlen(out) - 1; i > 0; i--) if (out[i] == '.') { out[i] = 0; break; }
}

/* convert interleaved PCM to mono s16 */
static s16 *pcm_to_mono(App *a, const u8 *p, u32 frames, int ch, int bits, int bigEndian, int isUnsigned8)
{
    s16 *d = (s16 *)mem_alloc(a, frames * 2 + 4, 0);
    u32 i;
    int bpf = (bits / 8) * ch, c;
    if (!d) return NULL;
    for (i = 0; i < frames; i++) {
        s32 acc = 0;
        for (c = 0; c < ch; c++) {
            const u8 *q = p + i * (u32)bpf + (u32)c * (u32)(bits / 8);
            s32 v;
            if (bits == 8) v = isUnsigned8 ? ((s32)q[0] - 128) * 256 : (s32)(s8)q[0] * 256;
            else if (bits == 16) v = bigEndian ? (s16)be16(q) : (s16)le16(q);
            else v = bigEndian ? (s16)be16(q) : (s16)le16(q + 1); /* 24-bit: top 16 bits */
            acc += v;
        }
        d[i] = (s16)(acc / ch);
    }
    return d;
}

static u32 ext80_to_u32(const u8 *p)
{
    s32 e = (s32)(be16(p) & 0x7FFF) - 16383;
    u32 m = be32(p + 2);
    if (e < 0 || e > 31) return 0;
    return m >> (31 - e);
}

int sample_load(App *a, const char *path, int slot)
{
    u32 len, rate = 16726, frames = 0, loopS = 0, loopL = 0;
    u8 *buf = read_whole(a, path, &len);
    s16 *d = NULL;
    Instr *in;
    if (!buf) return 0;
    if (slot < 1 || slot >= MAX_INSTR) { mem_free(a, buf); return 0; }
    in = &a->song->instr[slot];
    if (len >= 12 && !memcmp(buf, "FORM", 4) && !memcmp(buf + 8, "8SVX", 4)) {
        u32 pos = 12, one = 0, rep = 0;
        while (pos + 8 <= len) {
            u32 csz = be32(buf + pos + 4);
            const u8 *c = buf + pos + 8;
            if (pos + 8 + csz > len) csz = len - pos - 8;
            if (!memcmp(buf + pos, "VHDR", 4) && csz >= 20) {
                one = be32(c); rep = be32(c + 4); rate = be16(c + 12);
                if (c[15]) { mem_free(a, buf); return -2; } /* compressed */
            } else if (!memcmp(buf + pos, "BODY", 4)) {
                frames = csz;
                if (one + rep && one + rep < frames) frames = one + rep;
                d = pcm_to_mono(a, c, frames, 1, 8, 1, 0);
            }
            pos += 8 + ((csz + 1) & ~1u);
        }
        if (rep > 2) { loopS = one; loopL = rep; }
    } else if (len >= 12 && !memcmp(buf, "RIFF", 4) && !memcmp(buf + 8, "WAVE", 4)) {
        u32 pos = 12; int ch = 1, bits = 16, fmtok = 0;
        while (pos + 8 <= len) {
            u32 csz = le32(buf + pos + 4);
            const u8 *c = buf + pos + 8;
            if (pos + 8 + csz > len) csz = len - pos - 8;
            if (!memcmp(buf + pos, "fmt ", 4) && csz >= 16) {
                u32 fmt = le16(c);
                ch = (int)le16(c + 2); rate = le32(c + 4); bits = (int)le16(c + 14);
                fmtok = (fmt == 1 || fmt == 0xFFFE) && ch >= 1 && ch <= 8 && (bits == 8 || bits == 16 || bits == 24);
            } else if (!memcmp(buf + pos, "data", 4) && fmtok) {
                frames = csz / (u32)(ch * (bits / 8));
                d = pcm_to_mono(a, c, frames, ch, bits, 0, 1);
            }
            pos += 8 + ((csz + 1) & ~1u);
        }
    } else if (len >= 12 && !memcmp(buf, "FORM", 4) && (!memcmp(buf + 8, "AIFF", 4) || !memcmp(buf + 8, "AIFC", 4))) {
        u32 pos = 12; int ch = 1, bits = 16;
        while (pos + 8 <= len) {
            u32 csz = be32(buf + pos + 4);
            const u8 *c = buf + pos + 8;
            if (pos + 8 + csz > len) csz = len - pos - 8;
            if (!memcmp(buf + pos, "COMM", 4) && csz >= 18) {
                ch = (int)be16(c); frames = be32(c + 2); bits = (int)be16(c + 6); rate = ext80_to_u32(c + 8);
            } else if (!memcmp(buf + pos, "SSND", 4) && csz >= 8 && ch >= 1 && (bits == 8 || bits == 16 || bits == 24)) {
                u32 avail = (csz - 8 - be32(c)) / (u32)(ch * (bits / 8));
                if (frames > avail) frames = avail;
                d = pcm_to_mono(a, c + 8 + be32(c), frames, ch, bits, 1, 0);
            }
            pos += 8 + ((csz + 1) & ~1u);
        }
    } else {
        frames = len;   /* raw signed 8-bit */
        d = pcm_to_mono(a, buf, frames, 1, 8, 1, 0);
    }
    mem_free(a, buf);
    if (!d || !frames) { if (d) mem_free(a, d); return -1; }
    if (rate < 1000 || rate > 96000) rate = 16726;
    instr_set_sample(a, in, d, frames, rate);
    if (loopL && loopS + loopL <= frames) { in->loopStart = loopS; in->loopLen = loopL; }
    base_name(path, in->name, sizeof(in->name));
    a->ui.dirty = 1;
    return 1;
}

/* Export: .8svx/.iff -> IFF 8SVX (8-bit), anything else -> 16-bit WAV */
int sample_save_iff(App *a, const char *path, int slot)
{
    Instr *in = &a->song->instr[slot];
    W *w;
    u32 k, n;
    int ok;
    if (!in->data || !in->length) return 0;
    w = (W *)mem_alloc(a, sizeof(W), 0);
    if (!w) return 0;
    w->a = a;
    w->fh = f_open(a, path, 1);
    if (!w->fh) { mem_free(a, w); return 0; }
    n = in->length;
    if (str_icmp_suffix(path, ".8svx") || str_icmp_suffix(path, ".iff")) {
        u32 one = in->loopLen ? in->loopStart : n, rep = in->loopLen;
        u32 body = (n + 1) & ~1u;
        w_bytes(w, "FORM", 4); w_u32(w, 4 + 8 + 20 + 8 + body);
        w_bytes(w, "8SVX", 4);
        w_bytes(w, "VHDR", 4); w_u32(w, 20);
        w_u32(w, one); w_u32(w, rep); w_u32(w, 32); w_u16(w, in->baseRate > 65535 ? 65535 : in->baseRate);
        w_u8(w, 1); w_u8(w, 0); w_u32(w, (u32)in->volume << 10);
        w_bytes(w, "BODY", 4); w_u32(w, n);
        for (k = 0; k < n; k++) w_u8(w, (u32)(u8)(s8)(in->data[k] >> 8));
        if (n & 1) w_u8(w, 0);
    } else {
        u8 h[44];
        u32 rate = in->baseRate, dl = n * 2;
        memcpy(h, "RIFF", 4);
        h[4] = (u8)(dl + 36); h[5] = (u8)((dl + 36) >> 8); h[6] = (u8)((dl + 36) >> 16); h[7] = (u8)((dl + 36) >> 24);
        memcpy(h + 8, "WAVEfmt ", 8);
        h[16] = 16; h[17] = h[18] = h[19] = 0; h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
        h[24] = (u8)rate; h[25] = (u8)(rate >> 8); h[26] = (u8)(rate >> 16); h[27] = 0;
        h[28] = (u8)(rate * 2); h[29] = (u8)((rate * 2) >> 8); h[30] = (u8)((rate * 2) >> 16); h[31] = 0;
        h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
        memcpy(h + 36, "data", 4);
        h[40] = (u8)dl; h[41] = (u8)(dl >> 8); h[42] = (u8)(dl >> 16); h[43] = (u8)(dl >> 24);
        w_bytes(w, h, 44);
        for (k = 0; k < n; k++) { u8 b[2]; b[0] = (u8)in->data[k]; b[1] = (u8)((u16)in->data[k] >> 8); w_bytes(w, b, 2); }
    }
    w_flush(w);
    f_close(a, w->fh);
    ok = !w->err;
    mem_free(a, w);
    return ok;
}
