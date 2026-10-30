/* Sequencer + software mixer.
   Runs inside the vertical-blank interrupt on the Amiga, so it must be fast
   and must never allocate memory or touch the OS. */
#include "app.h"
#include "tables.inc"

static u32 mul1616(u32 a, u32 b)
{
    u32 ah = a >> 16, al = a & 0xFFFF, bh = b >> 16, bl = b & 0xFFFF;
    return ((ah * bh) << 16) + ah * bl + al * bh + ((al * bl) >> 16);
}

/* pitch: 1/64 semitone units relative to C-3. Returns 16.16 step. */
u32 pitch_to_step(s32 pitch, u32 baseRate)
{
    s32 oct, rem;
    u32 base, step;
    if (pitch < -768 * 8) pitch = -768 * 8;
    if (pitch > 768 * 6) pitch = 768 * 6;
    oct = pitch / 768; rem = pitch - oct * 768;
    if (rem < 0) { rem += 768; oct--; }
    if (baseRate > 131071) baseRate = 131071;
    base = ((baseRate << 15) / OUT_RATE) << 1;
    step = mul1616(base, pitch_tab[rem]);
    if (oct > 0) { while (oct-- > 0) { if (step > 0x01000000) break; step <<= 1; } }
    else if (oct < 0) step >>= (u32)(-oct);
    if (step > 0x00400000) step = 0x00400000;  /* 64x max */
    return step;
}

static void chan_update_gain(App *a, Chan *c, int track)
{
    Song *s = a->song;
    s32 pan = 128, v;
    if (track >= 0) pan = s->trackPan[track];
    if (c->ins) pan += (s32)c->ins->pan - 128;
    pan += (s32)c->pan - 128;
    if (pan < 0) pan = 0;
    if (pan > 255) pan = 255;
    v = (s32)c->vol * (s32)s->masterVol;            /* 0..4096 */
    if (track >= 0 && s->trackMute[track]) v = 0;
    c->gainL = (v * (256 - pan)) >> 10;               /* 0..1024 */
    c->gainR = (v * (pan + 1)) >> 10;
}

static void chan_update_step(Chan *c)
{
    if (!c->ins) return;
    c->step = pitch_to_step(c->pitch + c->vibOffset + c->arpOffset, c->ins->baseRate);
}

static void chan_start(App *a, Chan *c, const Instr *in, int note, u32 offset)
{
    u32 start = 0, end = in->length;
    c->ins = in;
    c->data = in->data;
    c->loopStart = 0; c->loopEnd = 0;
    if (in->nslices) {
        int sl = note - in->sliceBase;
        if (sl < 0 || sl >= in->nslices) { c->active = 0; return; }
        start = in->slice[sl];
        end = (sl + 1 < in->nslices) ? in->slice[sl + 1] : in->length;
        c->pitch = (s32)in->transpose * 64 + (s32)in->finetune * 8;
    } else {
        c->pitch = ((s32)note - NOTE_C3 + in->transpose) * 64 + (s32)in->finetune * 8;
        if (in->loopLen) {
            c->loopStart = in->loopStart;
            c->loopEnd = in->loopStart + in->loopLen;
            if (c->loopEnd > in->length) c->loopEnd = in->length;
            end = c->loopEnd;
        }
    }
    c->startPos = start;
    c->pos = start + offset;
    c->frac = 0;
    c->end = end;
    c->vibOffset = 0; c->arpOffset = 0;
    c->active = (c->data && c->pos < c->end);
    (void)a;
}

void player_forget_instr(App *a, const Instr *in)
{
    int t;
    for (t = 0; t < MAX_TRACKS; t++) if (a->pl.ch[t].ins == in) { a->pl.ch[t].active = 0; a->pl.ch[t].ins = NULL; a->pl.ch[t].data = NULL; }
    if (a->pl.preview.ins == in) { a->pl.preview.active = 0; a->pl.preview.ins = NULL; a->pl.preview.data = NULL; }
}

void player_set_tempo(App *a, int bpm)
{
    Player *p = &a->pl;
    if (bpm < 32) bpm = 32;
    if (bpm > 255) bpm = 255;
    p->bpm = bpm;
    p->tickLen = (u32)(OUT_RATE * 5) / (u32)(bpm * 2);
}

void player_init(App *a)
{
    Player *p = &a->pl;
    memset(p, 0, sizeof(Player));
    p->speed = 6;
    player_set_tempo(a, 125);
    p->breakTo = -1; p->jumpTo = -1;
}

static int seq_block(App *a)
{
    Song *s = a->song;
    Player *p = &a->pl;
    if (p->mode == PLAY_BLOCK) return p->block;
    if (p->orderPos >= s->orderLen) return -1;
    {
        const Section *sc = &s->sections[s->order[p->orderPos]];
        if (p->secSlot >= sc->count) return -1;
        return sc->blocks[p->secSlot];
    }
}

/* move to the next block in the timeline; returns 0 if nothing playable */
static int seq_next_block(App *a)
{
    Song *s = a->song;
    Player *p = &a->pl;
    int guard;
    if (p->mode == PLAY_BLOCK) return 1;
    p->secSlot++;
    for (guard = 0; guard < MAX_ORDER + 2; guard++) {
        if (p->orderPos < s->orderLen && p->secSlot < s->sections[s->order[p->orderPos]].count) return 1;
        p->secSlot = 0;
        p->orderPos++;
        if (p->orderPos >= s->orderLen) p->orderPos = 0;
        if (s->orderLen == 0) return 0;
        if (s->sections[s->order[p->orderPos]].count) return 1;
    }
    return 0;
}

static void trigger_cell(App *a, Chan *c, const Cell *cell, int track)
{
    Song *s = a->song;
    Player *p = &a->pl;
    int note = cell->note;
    int cmd = cell->cmd, prm = cell->param;
    int porta = (cmd == 0x03 || cmd == 0x05);
    const Instr *in;

    c->cmd = (u8)cmd; c->param = (u8)prm;
    c->arpOffset = 0;

    if (cmd == 0x1F && (prm >> 4) && note) {   /* note delay */
        c->pending = *cell;
        c->delay = (u8)(prm >> 4);
        c->retrig = (u8)(prm & 15);
        return;
    }
    c->delay = 0;
    c->retrig = (cmd == 0x1F) ? (u8)(prm & 15) : 0;

    if (cell->instr) {
        c->instrNum = cell->instr;
        in = &s->instr[cell->instr];
        c->vol = in->volume;
        c->pan = 128;
    }
    in = &s->instr[c->instrNum];

    if (note == NOTE_OFF) {
        c->active = 0;
    } else if (note >= 1 && note <= NOTE_MAX && c->instrNum) {
        if (porta && c->active && c->ins == in && !in->nslices) {
            c->targetPitch = ((s32)note - NOTE_C3 + in->transpose) * 64 + (s32)in->finetune * 8;
        } else if (in->data && in->length) {
            u32 ofs = (cmd == 0x19) ? (u32)prm << 8 : 0;
            chan_start(a, c, in, note, ofs);
            c->targetPitch = c->pitch;
        }
    }

    switch (cmd) {
    case 0x01: case 0x02: if (prm) c->slideSpeed = (u16)prm; break;
    case 0x03: if (prm) c->portaSpeed = (u16)prm; break;
    case 0x04:
        if (prm >> 4) c->vibSpeed = (u8)(prm >> 4);
        if (prm & 15) c->vibDepth = (u8)(prm & 15);
        break;
    case 0x08: c->pan = (u8)prm; break;
    case 0x09: if (prm >= 1 && prm <= 32) p->speed = prm; break;
    case 0x0B: p->jumpTo = prm; break;
    case 0x0C: c->vol = (u8)(prm > 64 ? 64 : prm); break;
    case 0x0D: if (prm) c->lastVolSlide = (u8)prm; break;
    case 0x0F:
        if (prm == 0) p->breakTo = 0;
        else if (prm <= 0xF0) player_set_tempo(a, prm < 32 ? 32 : prm);
        else if (prm == 0xFE) p->stopReq = 1;
        else if (prm == 0xFF) c->active = 0;
        break;
    }
    chan_update_step(c);
    chan_update_gain(a, c, track);
}

static void vol_slide(Chan *c, int prm)
{
    int up = prm >> 4, dn = prm & 15, v = c->vol;
    if (up) v += up; else v -= dn;
    if (v < 0) v = 0;
    if (v > 64) v = 64;
    c->vol = (u8)v;
}

static void tick_effects(App *a, Chan *c, int track, int tick)
{
    int prm = c->param;
    if (c->delay && tick == c->delay) {
        Cell cell = c->pending;
        cell.cmd = 0; cell.param = 0;
        c->delay = 0;
        trigger_cell(a, c, &cell, track);
        return;
    }
    if (c->retrig && tick % c->retrig == 0 && c->ins) { c->pos = c->startPos; c->frac = 0; c->active = c->data != NULL; }
    switch (c->cmd) {
    case 0x00:
        if (prm) {
            int k = tick % 3;
            c->arpOffset = k == 0 ? 0 : (k == 1 ? (prm >> 4) * 64 : (prm & 15) * 64);
        }
        break;
    case 0x01: c->pitch += c->slideSpeed * 2; break;
    case 0x02: c->pitch -= c->slideSpeed * 2; break;
    case 0x05: vol_slide(c, prm); /* fall through */
    case 0x03:
        if (c->pitch < c->targetPitch) { c->pitch += c->portaSpeed * 2; if (c->pitch > c->targetPitch) c->pitch = c->targetPitch; }
        else if (c->pitch > c->targetPitch) { c->pitch -= c->portaSpeed * 2; if (c->pitch < c->targetPitch) c->pitch = c->targetPitch; }
        break;
    case 0x06: vol_slide(c, prm); /* fall through */
    case 0x04:
        c->vibPos = (u8)((c->vibPos + c->vibSpeed) & 31);
        c->vibOffset = ((s32)vib_tab[c->vibPos] * c->vibDepth) >> 3;
        break;
    case 0x0D: vol_slide(c, c->lastVolSlide); break;
    }
    chan_update_step(c);
    chan_update_gain(a, c, track);
}

static void play_line(App *a)
{
    Song *s = a->song;
    Player *p = &a->pl;
    int t, b = seq_block(a);
    if (b < 0 || !s->blocks[b].used) { p->stopReq = 1; return; }
    p->block = b;
    if (p->line >= s->blocks[b].lines) p->line = 0;
    p->curLine = p->line; p->curBlock = b; p->curOrder = p->orderPos;
    for (t = 0; t < s->tracks; t++) {
        Chan *c = &p->ch[t];
        c->vibOffset = 0;
        trigger_cell(a, c, block_cell(s, b, p->line, t), t);
    }
}

static void advance_line(App *a)
{
    Song *s = a->song;
    Player *p = &a->pl;
    if (p->jumpTo >= 0) {
        p->orderPos = p->jumpTo < s->orderLen ? p->jumpTo : 0;
        p->secSlot = 0; p->line = 0;
        p->jumpTo = -1; p->breakTo = -1;
        if (p->mode == PLAY_SONG && s->orderLen && !s->sections[s->order[p->orderPos]].count) seq_next_block(a);
        return;
    }
    if (p->breakTo >= 0) {
        p->breakTo = -1; p->line = 0;
        if (!seq_next_block(a)) p->stopReq = 1;
        return;
    }
    p->line++;
    if (p->line >= s->blocks[p->block].lines) {
        p->line = 0;
        if (!seq_next_block(a)) p->stopReq = 1;
    }
}

static void player_tick(App *a)
{
    Player *p = &a->pl;
    Song *s = a->song;
    int t;
    if (p->mode == PLAY_STOP) return;
    if (p->tick == 0) play_line(a);
    else for (t = 0; t < s->tracks; t++) tick_effects(a, &p->ch[t], t, p->tick);
    p->tick++;
    if (p->tick >= p->speed) { p->tick = 0; advance_line(a); }
    if (p->stopReq) {
        p->mode = PLAY_STOP;
        for (t = 0; t < MAX_TRACKS; t++) p->ch[t].active = 0;
    }
}

void player_play(App *a, int mode, int fromCursor)
{
    Player *p = &a->pl;
    Song *s = a->song;
    int t;
    audio_lock(a);
    for (t = 0; t < MAX_TRACKS; t++) { memset(&p->ch[t], 0, sizeof(Chan)); }
    p->speed = s->speed;
    player_set_tempo(a, s->bpm);
    p->tick = 0; p->breakTo = -1; p->jumpTo = -1; p->stopReq = 0;
    p->tickLeft = 0;
    p->block = a->ui.block;
    p->line = fromCursor ? a->ui.line : 0;
    if (mode == PLAY_SONG) {
        p->orderPos = a->ui.ordCur < s->orderLen ? a->ui.ordCur : 0;
        p->secSlot = 0;
        if (fromCursor && s->orderLen) {
            /* start from the current block if it is in the current section */
            const Section *sc = &s->sections[s->order[p->orderPos]];
            int j;
            p->line = 0;
            for (j = 0; j < sc->count; j++) if (sc->blocks[j] == a->ui.block) { p->secSlot = j; p->line = a->ui.line; break; }
        }
        if (s->orderLen == 0) { audio_unlock(a); return; }
        if (!s->sections[s->order[p->orderPos]].count && !seq_next_block(a)) { audio_unlock(a); return; }
    }
    p->mode = mode;
    audio_unlock(a);
}

void player_stop(App *a)
{
    int t;
    audio_lock(a);
    a->pl.mode = PLAY_STOP;
    for (t = 0; t < MAX_TRACKS; t++) a->pl.ch[t].active = 0;
    audio_unlock(a);
}

void player_preview(App *a, int instr, int note, u32 start, u32 end)
{
    Chan *c = &a->pl.preview;
    const Instr *in = &a->song->instr[instr];
    if (!in->data || !in->length) return;
    audio_lock(a);
    memset(c, 0, sizeof(Chan));
    c->vol = in->volume; c->pan = 128; c->instrNum = (u8)instr;
    chan_start(a, c, in, note, 0);
    if (end > start && end <= in->length) {
        c->pos = start; c->startPos = start; c->end = end; c->loopEnd = 0;
        c->active = 1;
    }
    chan_update_step(c);
    chan_update_gain(a, c, -1);
    audio_unlock(a);
}

void player_preview_stop(App *a)
{
    audio_lock(a);
    a->pl.preview.active = 0;
    audio_unlock(a);
}

static void mix_chan(Chan *c, s32 *L, s32 *R, int n, int interp)
{
    const s16 *d = c->data;
    u32 pos = c->pos, frac = c->frac, step = c->step, end = c->end;
    s32 gl = c->gainL, gr = c->gainR;
    int i;
    if (!d) { c->active = 0; return; }
    for (i = 0; i < n; i++) {
        s32 smp = d[pos];
        if (interp && pos + 1 < end) {
            s32 nx = d[pos + 1];
            smp += ((nx - smp) * (s32)(frac >> 1)) >> 15;
        }
        L[i] += smp * gl;
        R[i] += smp * gr;
        frac += step;
        pos += frac >> 16;
        frac &= 0xFFFF;
        if (pos >= end) {
            if (c->loopEnd > c->loopStart) {
                u32 ll = c->loopEnd - c->loopStart;
                while (pos >= c->loopEnd) pos -= ll;
            } else { c->active = 0; break; }
        }
    }
    c->pos = pos; c->frac = frac;
}

void player_render(App *a, s16 *outL, s16 *outR, int frames)
{
    Player *p = &a->pl;
    Song *s = a->song;
    int done = 0, i, t;
    while (done < frames) {
        int n = frames - done;
        if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
        if (p->mode != PLAY_STOP) {
            if (p->tickLeft == 0) { player_tick(a); p->tickLeft = p->tickLen; }
            if ((u32)n > p->tickLeft) n = (int)p->tickLeft;
        }
        for (i = 0; i < n; i++) { p->mixL[i] = 0; p->mixR[i] = 0; }
        for (t = 0; t < MAX_TRACKS; t++)
            if (p->ch[t].active) mix_chan(&p->ch[t], p->mixL, p->mixR, n, s->interp);
        if (p->preview.active) mix_chan(&p->preview, p->mixL, p->mixR, n, s->interp);
        for (i = 0; i < n; i++) {
            s32 l = p->mixL[i] >> 10, r = p->mixR[i] >> 10;
            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            outL[done + i] = (s16)l;
            outR[done + i] = (s16)r;
        }
        if (p->mode != PLAY_STOP) p->tickLeft -= (u32)n;
        done += n;
    }
}
