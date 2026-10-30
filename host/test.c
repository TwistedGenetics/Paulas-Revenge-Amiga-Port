#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/app.h"
#include "../src/ui_int.h"
void host_key(App *a, int code, int qual);
void host_mouse(App *a, int type, int down, int x, int y);
void host_type(App *a, const char *s);
void host_dump(App *a, const char *path);
void host_wav(App *a, const char *path, int seconds);

static s16 *synth(int kind, u32 n, u32 rate)
{
    s16 *d = calloc(n, 2); u32 i; double ph = 0;
    for (i = 0; i < n; i++) {
        double t = (double)i / rate, v;
        if (kind == 0) { double f = 45 + 120 * exp(-t * 30); ph += 2 * M_PI * f / rate; v = sin(ph) * exp(-t * 6); }
        else if (kind == 1) { v = ((rand() / (double)RAND_MAX) * 2 - 1) * exp(-t * 18) * 0.8 + sin(2 * M_PI * 190 * t) * exp(-t * 25) * 0.5; }
        else if (kind == 2) { v = ((rand() / (double)RAND_MAX) * 2 - 1) * exp(-t * 60) * 0.5; }
        else { ph += 2 * M_PI * 55 / rate; v = sin(ph) * 0.9; }
        d[i] = (s16)(v * 30000);
    }
    return d;
}

static const char *outdir = ".";
static const char *OUT(const char *name) { static char buf[16][512]; static int k; k = (k + 1) & 15; snprintf(buf[k], 512, "%s/%s", outdir, name); return buf[k]; }

static void frame(App *a) { ui_frame(a); }
static int check(int cond, const char *what) { printf("%s %s\n", cond ? "PASS" : "FAIL", what); return cond; }

int main(int argc, char **argv)
{
    App *a = calloc(1, sizeof(App));
    int p, l, fails = 0;
    Song *s;
    if (argc > 1) outdir = argv[1];
    for (p = 0; p < 3; p++) a->plane[p] = calloc(ROWBYTES, SCREEN_H);
    if (!ui_init(a)) { puts("init failed"); return 1; }
    s = a->song;
    instr_set_sample(a, &s->instr[1], synth(0, 12000, 16726), 12000, 16726); strcpy(s->instr[1].name, "kick");
    instr_set_sample(a, &s->instr[2], synth(1, 9000, 16726), 9000, 16726); strcpy(s->instr[2].name, "snare");
    instr_set_sample(a, &s->instr[3], synth(2, 3000, 16726), 3000, 16726); strcpy(s->instr[3].name, "hat");
    instr_set_sample(a, &s->instr[4], synth(3, 16726, 16726), 16726, 16726); strcpy(s->instr[4].name, "sub");
    s->instr[4].loopStart = 0; s->instr[4].loopLen = 16726 / 55 * 55;

    /* type a pattern through the UI: edit on, notes */
    frame(a);
    host_key(a, K_SPACE, 0);              /* edit on */
    host_key(a, K_Z, 0);                  /* kick C-3 on line 0, step 1 -> line 1 */
    frame(a);
    fails += !check(block_cell(s, 0, 0, 0)->note == NOTE_C3 && block_cell(s, 0, 0, 0)->instr == 1, "note entry via keyboard");
    /* write directly for the rest */
    for (l = 0; l < 64; l++) {
        Cell *c;
        if (l % 16 == 0 || l % 16 == 10) { c = block_cell_edit(s, 0, l, 0); c->note = NOTE_C3; c->instr = 1; }
        if (l % 16 == 4 || l % 16 == 12) { c = block_cell_edit(s, 0, l, 1); c->note = NOTE_C3; c->instr = 2; }
        if (l % 2 == 0) { c = block_cell_edit(s, 0, l, 2); c->note = NOTE_C3 + 12; c->instr = 3; c->cmd = 0x0C; c->param = (l % 4) ? 0x18 : 0x30; }
        if (l % 32 == 0) { c = block_cell_edit(s, 0, l, 3); c->note = NOTE_C3 - 12; c->instr = 4; }
        if (l % 32 == 16) { c = block_cell_edit(s, 0, l, 3); c->note = NOTE_C3 - 9; c->instr = 4; c->cmd = 0x03; c->param = 0x08; }
        if (l % 32 == 28) { c = block_cell_edit(s, 0, l, 3); c->note = NOTE_OFF; }
    }
    strcpy(s->blocks[0].name, "beat A");
    /* variation: change only track 1 */
    {
        int v = block_make_variation(a, s, 0);
        Cell *c;
        fails += !check(v == 1 && block_track_linked(s, v, 0), "variation created + linked");
        c = block_cell_edit(s, v, 6, 1); c->note = NOTE_C3; c->instr = 2; c->cmd = 0x1F; c->param = 0x02;
        fails += !check(!block_track_linked(s, v, 1) && block_track_linked(s, v, 0), "edit breaks only that track's link");
        block_cell_edit(s, 0, 2, 0)->note = NOTE_C3 + 2; block_cell_edit(s, 0, 2, 0)->instr = 1;
        fails += !check(block_cell(s, v, 2, 0)->note == NOTE_C3 + 2, "parent edits flow into linked variation");
        fails += !check(block_cell(s, v, 6, 1)->cmd == 0x1F && block_cell(s, 0, 6, 1)->note == 0, "variation edit doesn't touch parent");
        strcpy(s->blocks[v].name, "beat B");
        {
            int sec = section_new(s, "Drop");
            s->sections[sec].blocks[0] = 0; s->sections[sec].blocks[1] = (u8)v; s->sections[sec].count = 2;
            strcpy(s->sections[0].name, "Intro");
            s->order[s->orderLen++] = (u8)sec;
            s->order[s->orderLen++] = 0;
        }
    }
    fails += !check(song_total_lines(s) == 64 * 4, "timeline total lines");
    /* render */
    a->ui.ordCur = 0;
    player_play(a, PLAY_SONG, 0);
    host_wav(a, OUT("song.wav"), 8);
    fails += !check(a->pl.mode == PLAY_SONG, "song still playing (loops)");
    player_stop(a);

    /* save / reload roundtrip */
    fails += !check(song_save(a, OUT("test.prv")) == 1, "save song");
    {
        int r = song_load(a, OUT("test.prv"));
        s = a->song;
        fails += !check(r == 1, "load song");
        fails += !check(s->orderLen == 3 && !strcmp(s->sections[1].name, "Drop"), "sections survive reload");
        fails += !check(block_track_linked(s, 1, 0) && block_cell(s, 1, 2, 0)->note == NOTE_C3 + 2, "links survive reload");
        fails += !check(s->instr[2].length == 9000 && s->instr[4].loopLen > 0, "samples survive reload");
    }
    /* sample tools */
    {
        Instr *in = &s->instr[2];
        int n;
        sample_save_iff(a, OUT("sn.wav"), 2);
        sample_save_iff(a, OUT("sn.8svx"), 2);
        fails += !check(sample_load(a, OUT("sn.wav"), 10) == 1 && s->instr[10].length == 9000, "WAV export/import");
        fails += !check(sample_load(a, OUT("sn.8svx"), 11) == 1 && s->instr[11].length == 9000, "8SVX export/import");
        st_resample(a, &s->instr[11], 12);
        fails += !check(s->instr[11].length > 4400 && s->instr[11].length < 4600, "resample +12 halves length");
        /* build a 'break' by concatenating hits */
        {
            u32 len = 16726 * 2, i; s16 *br = calloc(len, 2);
            int hits[8] = {0, 2, 4, 6, 9, 10, 12, 14}; int k;
            for (k = 0; k < 8; k++) { const Instr *src = &s->instr[(k == 2 || k == 6) ? 2 : 1]; u32 at = (u32)hits[k] * len / 16;
                for (i = 0; i < src->length && at + i < len; i++) br[at + i] = (s16)((br[at + i] + src->data[i]) / 1); }
            instr_set_sample(a, &s->instr[5], br, len, 16726); strcpy(s->instr[5].name, "break");
        }
        n = st_chop_transient(a, &s->instr[5], 5);
        printf("  transient slices: %d\n", n);
        fails += !check(n >= 6 && n <= 10, "transient chop finds the hits");
        n = st_chop_equal(a, &s->instr[5], 16);
        fails += !check(n == 16 && s->instr[5].slice[8] + 16 >= s->instr[5].length / 2 && s->instr[5].slice[8] <= s->instr[5].length / 2, "equal chop");
        fails += !check(st_slices_to_instruments(a, 5) == 16, "slices to instruments");
        st_normalise(a, in, 0, in->length);
        { s32 pk = 0; u32 i; for (i = 0; i < in->length; i++) { s32 v = in->data[i] < 0 ? -in->data[i] : in->data[i]; if (v > pk) pk = v; } fails += !check(pk >= 32700, "normalise"); }
    }
    /* play chopped break across keys */
    {
        int line;
        int b = block_new(a, s, 16);
        for (line = 0; line < 16; line++) { Cell *c = block_cell_edit(s, b, line, 0); c->note = (u8)(NOTE_C3 + ((line * 5) % 16)); c->instr = 5; }
        a->ui.block = b;
        player_play(a, PLAY_BLOCK, 0);
        host_wav(a, OUT("chop.wav"), 4);
        player_stop(a);
    }
    /* screenshots */
    a->ui.block = 0; a->ui.line = 8; a->ui.edit = 0;
    a->ui.instr = 5;
    frame(a); host_dump(a, OUT("s_block.ppm"));
    a->ui.block = 1; frame(a); host_dump(a, OUT("s_var.ppm"));
    host_key(a, K_F7, 0); frame(a); host_key(a, K_RIGHT, 0); frame(a); host_dump(a, OUT("s_arr.ppm"));
    host_key(a, K_F8, 0); frame(a);
    host_mouse(a, EV_MOUSE, 1, 160, 120); host_mouse(a, EV_MOVE, 0, 320, 120); host_mouse(a, EV_MOUSE, 0, 320, 120); frame(a);
    host_dump(a, OUT("s_smp.ppm"));
    host_key(a, K_F9, 0); frame(a); host_dump(a, OUT("s_ins.ppm"));
    host_key(a, K_HELP, 0); frame(a); host_dump(a, OUT("s_help.ppm"));
    host_key(a, K_HELP, 0); host_key(a, K_F10, 0); frame(a); host_dump(a, OUT("s_disk.ppm"));
    host_key(a, K_ESC, 0); host_type(a, "y"); host_key(a, K_RETURN, 0); frame(a);
    fails += !check(!a->ui.running, "quit prompt");
    printf("%d failures\n", fails);
    return fails != 0;
}
