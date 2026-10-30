/* Sample editor: waveform, mouse range selection, tools, chop, sampler input. */
#include "ui_int.h"

#define WAVE_Y0 (4 * FONT_H)
#define WAVE_Y1 (21 * FONT_H - 2)
#define WAVE_MID ((WAVE_Y0 + WAVE_Y1) / 2)
#define WAVE_HALF ((WAVE_Y1 - WAVE_Y0) / 2 - 1)

static void fix_view(App *a)
{
    Ui *u = &a->ui;
    Instr *in = cur_instr(a);
    if (!in->length) { u->viewStart = 0; u->viewLen = 0; u->rangeA = u->rangeB = 0; return; }
    if (u->viewLen < 64) u->viewLen = in->length < 64 ? in->length : 64;
    if (u->viewLen > in->length) u->viewLen = in->length;
    if (u->viewStart + u->viewLen > in->length) u->viewStart = in->length - u->viewLen;
    if (u->rangeA > in->length) u->rangeA = in->length;
    if (u->rangeB > in->length) u->rangeB = in->length;
}

static u32 x_to_frame(App *a, int x)
{
    Ui *u = &a->ui;
    if (x < 0) x = 0;
    if (x > SCREEN_W - 1) x = SCREEN_W - 1;
    return u->viewStart + (u32)(((u->viewLen >> 4) * (u32)x / SCREEN_W) << 4) + (u32)((u->viewLen & 15) * (u32)x / SCREEN_W);
}

static int frame_to_x(App *a, u32 f)
{
    Ui *u = &a->ui;
    if (!u->viewLen) return -1;
    if (f < u->viewStart || f > u->viewStart + u->viewLen) return -1;
    return (int)(((f - u->viewStart) >> 4) * SCREEN_W / ((u->viewLen >> 4) + 1));
}

static void range(App *a, u32 *s, u32 *e)
{
    Ui *u = &a->ui;
    Instr *in = cur_instr(a);
    u32 x = u->rangeA, y = u->rangeB;
    if (x > y) { u32 t = x; x = y; y = t; }
    if (x == y) { x = 0; y = in->length; }
    *s = x; *e = y;
}

void instr_backup(App *a)
{
    Ui *u = &a->ui;
    Instr *in = cur_instr(a);
    if (u->undo.data) { mem_free(a, u->undo.data); u->undo.data = NULL; }
    u->undoValid = 0;
    if (!in->data) return;
    u->undo = *in;
    u->undo.data = (s16 *)mem_alloc(a, in->length * 2 + 4, 0);
    if (!u->undo.data) return;
    memcpy(u->undo.data, in->data, in->length * 2);
    u->undoSlot = u->instr;
    u->undoValid = 1;
}

void sample_undo(App *a)
{
    Ui *u = &a->ui;
    Instr *in, tmp;
    if (!u->undoValid) { ui_status(a, "Nothing to undo."); return; }
    in = &a->song->instr[u->undoSlot];
    audio_lock(a);
    player_forget_instr(a, in);
    tmp = *in; *in = u->undo; u->undo = tmp;
    audio_unlock(a);
    u->instr = u->undoSlot;
    fix_view(a);
    ui_status(a, "Undone (press U again to redo).");
}

static void draw_wave(App *a)
{
    Ui *u = &a->ui;
    Instr *in = cur_instr(a);
    int x;
    u32 s0, s1;
    gfx_hline(a, 0, SCREEN_W - 1, WAVE_Y0 - 1, 0);
    gfx_hline(a, 0, SCREEN_W - 1, WAVE_Y1 + 1, 0);
    if (!in->data || !in->length) {
        gfx_text(a, 24, 12, "No sample. F10 to load, R to record.", 0);
        return;
    }
    /* loop region, selection */
    if (in->loopLen) {
        int lx0 = frame_to_x(a, in->loopStart), lx1 = frame_to_x(a, in->loopStart + in->loopLen);
        if (lx0 < 0) lx0 = in->loopStart < u->viewStart ? 0 : SCREEN_W;
        if (lx1 < 0) lx1 = in->loopStart + in->loopLen > u->viewStart + u->viewLen ? SCREEN_W - 1 : -1;
        if (lx1 >= lx0) gfx_rect_fill(a, lx0, WAVE_Y0, lx1, WAVE_Y1, 1, 1);
    }
    if (u->rangeA != u->rangeB) {
        range(a, &s0, &s1);
        {
            int x0 = s0 < u->viewStart ? 0 : frame_to_x(a, s0);
            int x1 = s1 > u->viewStart + u->viewLen ? SCREEN_W - 1 : frame_to_x(a, s1);
            if (x0 >= 0 && x1 >= x0) gfx_rect_fill(a, x0, WAVE_Y0, x1, WAVE_Y1, 2, 1);
        }
    }
    gfx_hline(a, 0, SCREEN_W - 1, WAVE_MID, 1);
    for (x = 0; x < SCREEN_W; x++) {
        u32 f0 = x_to_frame(a, x), f1 = x_to_frame(a, x + 1), f, stride;
        s32 mn = 32767, mx = -32768;
        if (f1 <= f0) f1 = f0 + 1;
        if (f1 > in->length) f1 = in->length;
        if (f0 >= in->length) break;
        stride = (f1 - f0) / 32 + 1;
        for (f = f0; f < f1; f += stride) { s32 v = in->data[f]; if (v < mn) mn = v; if (v > mx) mx = v; }
        gfx_vline(a, x, WAVE_MID - (mx * WAVE_HALF >> 15), WAVE_MID - (mn * WAVE_HALF >> 15), 0);
    }
    /* slice markers */
    {
        int i;
        for (i = 0; i < in->nslices; i++) {
            int sx = frame_to_x(a, in->slice[i]), y;
            char num[4];
            if (sx < 0) continue;
            for (y = WAVE_Y0; y <= WAVE_Y1; y += 2) gfx_pset(a, sx, y, 0);
            fmt_dec(num, (u32)i + 1, 1);
            if (sx / 8 < COLS - 2) gfx_text(a, sx / 8, 3, num, 0);
        }
    }
}

static void draw_monitor(App *a)
{
    Ui *u = &a->ui;
    int x, peak = 0, prev = WAVE_MID;
    gfx_text(a, 30, 3, "SAMPLER INPUT MONITOR", 0);
    for (x = 0; x < SCREEN_W; x++) {
        s32 v = sampler_peek(a, u->recChan);
        int y = WAVE_MID - (v * WAVE_HALF >> 15);
        if (v < 0) v = -v;
        if (v > peak) peak = (int)v;
        gfx_vline(a, x, prev, y, 0);
        prev = y;
    }
    gfx_rect_fill(a, 0, WAVE_Y1 - 6, (peak * (SCREEN_W - 1)) >> 15, WAVE_Y1 - 2, 2, 1);
    if (peak > 32000) gfx_text(a, 70, 20, "CLIP!", 0);
}

static void do_record(App *a)
{
    Ui *u = &a->ui;
    u32 frames = (u32)u->recRate * (u32)u->recSeconds, got;
    s16 *buf;
    Instr *in = cur_instr(a);
    player_stop(a);
    buf = (s16 *)mem_alloc(a, frames * 2 + 4, 0);
    if (!buf) { ui_status(a, "Not enough memory for that recording."); return; }
    instr_backup(a);
    got = sampler_record(a, buf, frames, (u32)u->recRate, u->recChan);
    if (got < 16) { mem_free(a, buf); ui_status(a, "Nothing recorded."); return; }
    instr_set_sample(a, in, buf, got, (u32)u->recRate);
    if (!in->name[0]) str_ncpy(in->name, "sampled", sizeof(in->name));
    u->viewStart = 0; u->viewLen = got; u->rangeA = u->rangeB = 0;
    u->dirty = 1;
    ui_status(a, "Recorded.");
}

void sample_draw(App *a)
{
    Ui *u = &a->ui;
    Instr *in = cur_instr(a);
    char *t = a->tmp;
    int n;
    u32 s0, s1;
    fix_view(a);
    draw_header(a, "SAMPLE EDITOR");
    n = 0;
    memcpy(t, "Instr ", 6); n = 6; n += fmt_hex(t + n, (u32)u->instr, 2);
    t[n++] = ' '; t[n++] = '"'; memcpy(t + n, in->name, strlen(in->name)); n += (int)strlen(in->name); t[n++] = '"';
    memcpy(t + n, "  Len ", 6); n += 6; n += fmt_dec(t + n, in->length, 1);
    memcpy(t + n, "  Rate ", 7); n += 7; n += fmt_dec(t + n, in->baseRate, 1);
    if (in->nslices) { memcpy(t + n, "  Slices ", 9); n += 9; n += fmt_dec(t + n, in->nslices, 1); memcpy(t + n, " from C-3", 9); n += 9; }
    t[n] = 0;
    gfx_text(a, 0, 1, t, 0);
    draw_fkeys(a, 0);

    if (u->recArm == 1) { u->recArm = 0; do_record(a); }
    if (u->recArm == 2) {
        u->recArm = 1;
        gfx_text(a, 18, 11, "RECORDING FROM PARALLEL PORT SAMPLER...", 0);
        gfx_text(a, 18, 13, "Click the LEFT MOUSE BUTTON to stop early.", 0);
        return;
    }
    if (u->monitor) draw_monitor(a);
    else draw_wave(a);

    range(a, &s0, &s1);
    n = 0;
    memcpy(t, "View ", 5); n = 5; n += fmt_dec(t + n, u->viewStart, 6); t[n++] = '-'; n += fmt_dec(t + n, u->viewStart + u->viewLen, 6);
    memcpy(t + n, "  Range ", 8); n += 8; n += fmt_dec(t + n, s0, 6); t[n++] = '-'; n += fmt_dec(t + n, s1, 6);
    memcpy(t + n, "  Loop ", 7); n += 7;
    if (in->loopLen) { n += fmt_dec(t + n, in->loopStart, 6); t[n++] = '+'; n += fmt_dec(t + n, in->loopLen, 1); }
    else { memcpy(t + n, "off", 3); n += 3; }
    t[n] = 0;
    gfx_text(a, 0, 21, t, 0);
    n = 0;
    memcpy(t, "Sampler: ", 9); n = 9;
    n += fmt_dec(t + n, (u32)u->recRate, 1); memcpy(t + n, " Hz, ", 5); n += 5;
    n += fmt_dec(t + n, (u32)u->recSeconds, 1); memcpy(t + n, " s, ", 4); n += 4;
    memcpy(t + n, u->recChan ? "right" : "left/mono", u->recChan ? 5 : 9); n += u->recChan ? 5 : 9;
    t[n] = 0;
    gfx_text(a, 0, 22, t, 0);
    gfx_text(a, 0, 23, "N norm V rev I/O fade S sil G gain C crop X cut L loop K unloop F xfade P pitch H/T chop", 0);
}

void sample_mouse(App *a, const Event *e)
{
    Ui *u = &a->ui;
    Instr *in = cur_instr(a);
    int x = e->x, y = e->y;
    if (!in->length || u->monitor) return;
    if (e->type == EV_MOUSE && e->code == 0) {
        if (e->down && y >= WAVE_Y0 && y <= WAVE_Y1) {
            u->rangeA = u->rangeB = x_to_frame(a, x);
            u->dragging = 1;
        } else if (!e->down) u->dragging = 0;
    } else if (e->type == EV_MOVE && u->dragging) {
        u->rangeB = x_to_frame(a, x);
    }
}

void sample_prompt_done(App *a, int kind, const char *txt)
{
    Ui *u = &a->ui;
    Instr *in = cur_instr(a);
    int v = 0, ok = ui_parse_int(txt, &v);
    u32 s0, s1;
    range(a, &s0, &s1);
    switch (kind) {
    case PR_GAIN:
        if (ok && v > 0 && v <= 800) { instr_backup(a); st_gain(a, in, s0, s1, v); u->dirty = 1; }
        break;
    case PR_RESAMPLE:
        if (ok && v >= -36 && v <= 36 && v) {
            instr_backup(a);
            if (st_resample(a, in, v)) { u->viewStart = 0; u->viewLen = in->length; u->rangeA = u->rangeB = 0; u->dirty = 1; ui_status(a, "Resampled."); }
            else ui_status(a, "Resample failed (memory?).");
        }
        break;
    case PR_CHOP_N:
        if (ok && v >= 2 && v <= MAX_SLICES) {
            st_chop_equal(a, in, v); u->dirty = 1;
            ui_status(a, "Chopped: slice 1 is on C-3, then one slice per key upward.");
        }
        break;
    case PR_CHOP_T:
        if (ok && v >= 1 && v <= 9) {
            int n = st_chop_transient(a, in, v);
            char *t = a->tmp;
            int k;
            u->dirty = 1;
            k = fmt_dec(t, (u32)n, 1);
            memcpy(t + k, " slices found, mapped from C-3 upward.", 39);
            ui_status(a, t);
        }
        break;
    case PR_XFADE:
        if (ok && v > 0 && in->loopLen) {
            instr_backup(a);
            st_loop_xfade(a, in, (u32)v * in->baseRate / 1000);
            u->dirty = 1; ui_status(a, "Loop crossfaded.");
        }
        break;
    case PR_SAVE_SAMPLE: {
        char *p = a->tmp;
        int l = (int)strlen(u->path);
        str_ncpy(p, u->path, 200);
        if (l && p[l - 1] != ':' && p[l - 1] != '/') { p[l++] = '/'; p[l] = 0; }
        str_ncpy(p + l, txt, 56);
        ui_status(a, sample_save_iff(a, p, u->instr) ? "Sample saved." : "Sample save failed!");
        break;
    }
    case PR_REC_RATE:
        if (ok && v >= 4000 && v <= 44100) u->recRate = v;
        fmt_dec(a->tmp, (u32)u->recSeconds, 1);
        ui_prompt(a, PR_REC_SECS, "Record length in seconds (1-60):", a->tmp);
        break;
    case PR_REC_SECS:
        if (ok && v >= 1 && v <= 60) u->recSeconds = v;
        u->monitor = 0;
        u->recArm = 2;
        break;
    }
}

void sample_key(App *a, const Event *e)
{
    Ui *u = &a->ui;
    Instr *in = cur_instr(a);
    int code = e->code, shift = (e->qual & Q_SHIFT) != 0;
    u32 s0, s1;
    char *t = a->tmp;
    range(a, &s0, &s1);

    switch (code) {
    case K_LBRACK: if (u->instr > 1) u->instr--; u->viewStart = 0; u->viewLen = cur_instr(a)->length; u->rangeA = u->rangeB = 0; return;
    case K_RBRACK: if (u->instr < MAX_INSTR - 1) u->instr++; u->viewStart = 0; u->viewLen = cur_instr(a)->length; u->rangeA = u->rangeB = 0; return;
    case K_M: u->monitor = !u->monitor; return;
    case K_R:
        fmt_dec(t, (u32)u->recRate, 1);
        ui_prompt(a, PR_REC_RATE, "Record rate Hz (4000-44100):", t);
        return;
    case K_Q: u->recChan = !u->recChan; return;
    case K_U: sample_undo(a); return;
    case K_W: ui_prompt(a, PR_SAVE_SAMPLE, "Save sample as (.8svx or .wav):", in->name); return;
    }
    if (!in->data || !in->length) return;

    switch (code) {
    case K_SPACE: player_preview(a, u->instr, NOTE_C3, s0, s1); return;
    case K_A: u->rangeA = 0; u->rangeB = in->length; return;
    case K_UP: {   /* zoom in around the range or the centre */
        u32 c = u->rangeA != u->rangeB ? (s0 + s1) / 2 : u->viewStart + u->viewLen / 2;
        u->viewLen /= 2;
        u->viewStart = c > u->viewLen / 2 ? c - u->viewLen / 2 : 0;
        fix_view(a); return;
    }
    case K_DOWN: {
        u32 c = u->viewStart + u->viewLen / 2;
        u->viewLen *= 2;
        u->viewStart = c > u->viewLen / 2 ? c - u->viewLen / 2 : 0;
        fix_view(a); return;
    }
    case K_LEFT: {
        u32 d = shift ? u->viewLen : u->viewLen / 8;
        u->viewStart = u->viewStart > d ? u->viewStart - d : 0; return;
    }
    case K_RIGHT: u->viewStart += shift ? u->viewLen : u->viewLen / 8; fix_view(a); return;
    case K_Z: if (u->rangeA != u->rangeB) { u->viewStart = s0; u->viewLen = s1 - s0; fix_view(a); } return;
    case K_N: instr_backup(a); st_normalise(a, in, s0, s1); u->dirty = 1; ui_status(a, "Normalised."); return;
    case K_V: instr_backup(a); st_reverse(a, in, s0, s1); u->dirty = 1; ui_status(a, "Reversed."); return;
    case K_I: instr_backup(a); st_fade(a, in, s0, s1, 0); u->dirty = 1; ui_status(a, "Faded in."); return;
    case K_O: instr_backup(a); st_fade(a, in, s0, s1, 1); u->dirty = 1; ui_status(a, "Faded out."); return;
    case K_S: instr_backup(a); st_silence(a, in, s0, s1); u->dirty = 1; ui_status(a, "Silenced."); return;
    case K_G: ui_prompt(a, PR_GAIN, "Gain percent (1-800):", "100"); return;
    case K_C:
        if (u->rangeA == u->rangeB) { ui_status(a, "Select a range first (drag with the mouse)."); return; }
        instr_backup(a);
        if (st_crop(a, in, s0, s1)) { u->viewStart = 0; u->viewLen = in->length; u->rangeA = u->rangeB = 0; u->dirty = 1; ui_status(a, "Cropped."); }
        return;
    case K_X:
        if (u->rangeA == u->rangeB) { ui_status(a, "Select a range first (drag with the mouse)."); return; }
        instr_backup(a);
        if (st_cut(a, in, s0, s1)) { u->rangeA = u->rangeB = s0; fix_view(a); u->dirty = 1; ui_status(a, "Cut."); }
        return;
    case K_L:
        if (u->rangeA == u->rangeB) { ui_status(a, "Select the loop with the mouse first."); return; }
        audio_lock(a); in->loopStart = s0; in->loopLen = s1 - s0; in->nslices = 0; audio_unlock(a);
        u->dirty = 1; ui_status(a, "Loop set to range."); return;
    case K_K: audio_lock(a); in->loopLen = 0; audio_unlock(a); u->dirty = 1; ui_status(a, "Loop off."); return;
    case K_F:
        if (!in->loopLen) { ui_status(a, "Set a loop first (L)."); return; }
        ui_prompt(a, PR_XFADE, "Loop crossfade length in ms:", "20"); return;
    case K_P: ui_prompt(a, PR_RESAMPLE, "Resample by semitones (-36..36, + = higher):", "12"); return;
    case K_H: ui_prompt(a, PR_CHOP_N, "Chop into how many equal slices? (2-64):", "16"); return;
    case K_T: ui_prompt(a, PR_CHOP_T, "Transient chop sensitivity (1-9):", "5"); return;
    case K_J: {
        int n = st_slices_to_instruments(a, u->instr);
        int k = fmt_dec(t, (u32)n, 1);
        memcpy(t + k, " slices copied to empty instrument slots.", 42);
        ui_status(a, n ? t : "No slices to copy (chop first with H or T).");
        if (n) u->dirty = 1;
        return;
    }
    case K_E:
        audio_lock(a); in->nslices = 0; audio_unlock(a);
        ui_status(a, "Slices removed."); return;
    }
    {
        int off = key_note_offset(code);
        if (off >= 0 && !(e->qual & Q_CTRL)) {
            int note = (u->octave - 1) * 12 + off + 1;
            if (note <= NOTE_MAX) player_preview(a, u->instr, note, 0, 0);
        }
    }
}
