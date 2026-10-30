/* Destructive sample tools. Ranges are [s, e) in frames. */
#include "app.h"

static int fix_range(const Instr *in, u32 *s, u32 *e)
{
    if (!in->data || !in->length) return 0;
    if (*e > in->length) *e = in->length;
    if (*s >= *e) { *s = 0; *e = in->length; }
    return 1;
}

static s16 clip16(s32 v) { return (s16)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }

void st_normalise(App *a, Instr *in, u32 s, u32 e)
{
    s32 peak = 1, scale;
    u32 i;
    (void)a;
    if (!fix_range(in, &s, &e)) return;
    for (i = s; i < e; i++) { s32 v = in->data[i]; if (v < 0) v = -v; if (v > peak) peak = v; }
    scale = (32767 << 12) / peak;   /* 4.12 */
    for (i = s; i < e; i++) in->data[i] = clip16((in->data[i] * scale) >> 12);
}

void st_reverse(App *a, Instr *in, u32 s, u32 e)
{
    (void)a;
    if (!fix_range(in, &s, &e)) return;
    e--;
    while (s < e) { s16 t = in->data[s]; in->data[s] = in->data[e]; in->data[e] = t; s++; e--; }
}

void st_fade(App *a, Instr *in, u32 s, u32 e, int out)
{
    u32 i, len;
    (void)a;
    if (!fix_range(in, &s, &e)) return;
    len = e - s;
    for (i = 0; i < len; i++) {
        u32 g = (i << 12) / len;          /* 0..4096 */
        if (out) g = 4096 - g;
        in->data[s + i] = (s16)(((s32)in->data[s + i] * (s32)g) >> 12);
    }
}

void st_silence(App *a, Instr *in, u32 s, u32 e)
{
    (void)a;
    if (!fix_range(in, &s, &e)) return;
    memset(in->data + s, 0, (e - s) * 2);
}

void st_gain(App *a, Instr *in, u32 s, u32 e, int percent)
{
    u32 i;
    s32 g = (percent << 12) / 100;
    (void)a;
    if (!fix_range(in, &s, &e)) return;
    for (i = s; i < e; i++) in->data[i] = clip16((in->data[i] * g) >> 12);
}

/* replace the sample buffer, keeping instrument settings */
static void swap_data(App *a, Instr *in, s16 *nd, u32 nlen)
{
    s16 *old = in->data;
    audio_lock(a);
    player_forget_instr(a, in);
    in->data = nd; in->length = nlen;
    if (in->loopStart >= nlen) { in->loopStart = 0; in->loopLen = 0; }
    if (in->loopStart + in->loopLen > nlen) in->loopLen = nlen - in->loopStart;
    in->nslices = 0;
    audio_unlock(a);
    if (old) mem_free(a, old);
}

int st_crop(App *a, Instr *in, u32 s, u32 e)
{
    s16 *nd;
    if (!fix_range(in, &s, &e)) return 0;
    nd = (s16 *)mem_alloc(a, (e - s) * 2 + 4, 0);
    if (!nd) return 0;
    memcpy(nd, in->data + s, (e - s) * 2);
    swap_data(a, in, nd, e - s);
    in->loopStart = 0; in->loopLen = 0;
    return 1;
}

int st_cut(App *a, Instr *in, u32 s, u32 e)
{
    s16 *nd;
    u32 nlen;
    if (!fix_range(in, &s, &e)) return 0;
    nlen = in->length - (e - s);
    if (nlen == 0) { swap_data(a, in, NULL, 0); return 1; }
    nd = (s16 *)mem_alloc(a, nlen * 2 + 4, 0);
    if (!nd) return 0;
    memcpy(nd, in->data, s * 2);
    memcpy(nd + s, in->data + e, (in->length - e) * 2);
    swap_data(a, in, nd, nlen);
    return 1;
}

/* Pitch-shift by resampling (changes length). Linear interpolation. */
int st_resample(App *a, Instr *in, int semitones)
{
    u32 step, nlen, i, pos, frac;
    s16 *nd;
    if (!in->data || !in->length || semitones == 0) return 0;
    /* step = 2^(semitones/12) in 16.16, reuse the player's table via a fake base rate */
    step = pitch_to_step(semitones * 64, OUT_RATE);
    if (step == 0) return 0;
    nlen = (u32)(((in->length >> 4) << 16) / step) << 4;
    if (nlen < 2) return 0;
    nd = (s16 *)mem_alloc(a, nlen * 2 + 4, 0);
    if (!nd) return 0;
    pos = 0; frac = 0;
    for (i = 0; i < nlen; i++) {
        s32 v0, v1;
        if (pos >= in->length) { nlen = i; break; }
        v0 = in->data[pos];
        v1 = (pos + 1 < in->length) ? in->data[pos + 1] : v0;
        nd[i] = (s16)(v0 + (((v1 - v0) * (s32)(frac >> 1)) >> 15));
        frac += step; pos += frac >> 16; frac &= 0xFFFF;
    }
    {
        u32 ls = in->loopStart, ll = in->loopLen;
        swap_data(a, in, nd, nlen);
        if (ll) {
            in->loopStart = (u32)(((ls >> 4) << 16) / step) << 4;
            in->loopLen = (u32)(((ll >> 4) << 16) / step) << 4;
            if (in->loopStart + in->loopLen > nlen) in->loopLen = nlen - in->loopStart;
        }
    }
    return 1;
}

/* Crossfade the end of the loop with the material before loop start. */
void st_loop_xfade(App *a, Instr *in, u32 xlen)
{
    u32 ls = in->loopStart, le = in->loopStart + in->loopLen, i;
    (void)a;
    if (!in->data || !in->loopLen) return;
    if (xlen > in->loopLen / 2) xlen = in->loopLen / 2;
    if (xlen > ls) xlen = ls;
    if (xlen < 2) return;
    for (i = 0; i < xlen; i++) {
        s32 g = (s32)((i << 12) / xlen);           /* 0..4096 across the fade */
        s32 tail = in->data[le - xlen + i];
        s32 pre = in->data[ls - xlen + i];
        in->data[le - xlen + i] = (s16)((tail * (4096 - g) + pre * g) >> 12);
    }
}

int st_chop_equal(App *a, Instr *in, int n)
{
    int i;
    (void)a;
    if (!in->data || !in->length) return 0;
    if (n < 2) n = 2;
    if (n > MAX_SLICES) n = MAX_SLICES;
    audio_lock(a);
    for (i = 0; i < n; i++) in->slice[i] = (u32)(((in->length >> 4) * (u32)i / (u32)n) << 4);
    in->nslices = (u8)n;
    in->sliceBase = NOTE_C3;
    in->loopLen = 0;
    audio_unlock(a);
    return n;
}

/* Energy-rise transient detector. sens 1..9 (higher = more slices). */
int st_chop_transient(App *a, Instr *in, int sens)
{
    u32 win = 256, i, j, minGap;
    u32 prev = 0, cnt = 0;
    u32 foundb[MAX_SLICES], *found = foundb;
    s32 ratioX16;
    OPAQUE(found);
    if (!in->data || in->length < win * 4) return 0;
    if (sens < 1) sens = 1;
    if (sens > 9) sens = 9;
    ratioX16 = 16 + (10 - sens) * 6;    /* energy must rise by this/16 */
    minGap = in->baseRate / 16;          /* ~62 ms */
    found[cnt++] = 0;
    for (i = win; i + win <= in->length && cnt < MAX_SLICES; i += win / 2) {
        u32 e = 0;
        for (j = 0; j < win; j += 2) { s32 v = in->data[i + j] >> 4; e += (u32)(v * v) >> 4; }
        if (prev > 64 && e * 16 > prev * (u32)ratioX16 && e > 2048 && i - found[cnt - 1] >= minGap) {
            /* refine to the first big sample in this window */
            u32 k = i;
            for (j = 0; j < win; j++) { s32 v = in->data[i + j]; if (v < 0) v = -v; if (v > 2000) { k = i + j; break; } }
            found[cnt++] = k > 32 ? k - 32 : k;
        }
        prev = e < 64 ? 64 : e;
    }
    audio_lock(a);
    for (i = 0; i < cnt; i++) in->slice[i] = found[i];
    in->nslices = (u8)cnt;
    in->sliceBase = NOTE_C3;
    in->loopLen = 0;
    audio_unlock(a);
    return (int)cnt;
}

/* Copy each slice of instrument src into the following empty instrument slots. */
int st_slices_to_instruments(App *a, int src)
{
    Song *s = a->song;
    Instr *in = &s->instr[src];
    int i, slot = 1, made = 0;
    if (!in->nslices) return 0;
    for (i = 0; i < in->nslices; i++) {
        u32 st = in->slice[i], en = (i + 1 < in->nslices) ? in->slice[i + 1] : in->length;
        s16 *nd;
        while (slot < MAX_INSTR && (s->instr[slot].data || slot == src)) slot++;
        if (slot >= MAX_INSTR) break;
        nd = (s16 *)mem_alloc(a, (en - st) * 2 + 4, 0);
        if (!nd) break;
        memcpy(nd, in->data + st, (en - st) * 2);
        instr_set_sample(a, &s->instr[slot], nd, en - st, in->baseRate);
        {
            char *nm = s->instr[slot].name;
            int l;
            str_ncpy(nm, in->name, 20);
            l = (int)strlen(nm);
            nm[l] = '.'; fmt_dec(nm + l + 1, (u32)i + 1, 2);
        }
        s->instr[slot].volume = in->volume;
        made++;
    }
    return made;
}
