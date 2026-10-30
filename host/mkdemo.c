/* Builds demo.prv: a short jungle sketch showing chop-to-keys, sections and linked variations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/app.h"

#define SR 16726
static double rnd(void) { return rand() / (double)RAND_MAX * 2 - 1; }
static void add_kick(float *b, int at, int n) { double ph = 0; int i; for (i = 0; i < 5000 && at + i < n; i++) { double t = (double)i / SR; ph += 2 * M_PI * (48 + 140 * exp(-t * 35)) / SR; b[at + i] += (float)(sin(ph) * exp(-t * 9) * 0.95); } }
static void add_snare(float *b, int at, int n, double g) { int i; for (i = 0; i < 4200 && at + i < n; i++) { double t = (double)i / SR; b[at + i] += (float)(g * (rnd() * exp(-t * 16) * 0.7 + sin(2 * M_PI * 185 * t) * exp(-t * 30) * 0.5)); } }
static void add_hat(float *b, int at, int n, double g) { int i; double lp = 0; for (i = 0; i < 1200 && at + i < n; i++) { double t = (double)i / SR, x = rnd(); double hp = x - lp; lp = x; b[at + i] += (float)(g * hp * exp(-t * 55) * 0.45); } }
static s16 *to16(float *b, int n) { s16 *d = malloc(n * 2); int i; float pk = 0; for (i = 0; i < n; i++) if (fabsf(b[i]) > pk) pk = fabsf(b[i]); for (i = 0; i < n; i++) d[i] = (s16)(b[i] / pk * 30000); return d; }
static void setins(App *a, int slot, const char *name, s16 *d, int n, int loopS, int loopL)
{
    Instr *in = &a->song->instr[slot];
    instr_set_sample(a, in, d, n, SR);
    strcpy(in->name, name);
    if (loopL) { in->loopStart = loopS; in->loopLen = loopL; }
}
static void cell(Song *s, int b, int l, int t, int note, int ins, int cmd, int prm) { Cell *c = block_cell_edit(s, b, l, t); c->note = note; c->instr = ins; c->cmd = cmd; c->param = prm; }

int main(int argc, char **argv)
{
    App *a = calloc(1, sizeof(App));
    Song *s; int i, l, p;
    for (p = 0; p < 3; p++) a->plane[p] = calloc(ROWBYTES, SCREEN_H);
    if (!ui_init(a)) return 1;
    s = a->song;
    strcpy(s->name, "Paula Dread");
    s->bpm = 170; s->speed = 6; s->tracks = 6; s->masterVol = 40;
    { /* 2-bar break, 32 sixteenths */
        int n = (int)(SR * 2 * 4 * 60.0 / 170.0), step = n / 32;
        float *b = calloc(n, sizeof(float));
        static const char pat[] = "K.k.S..kK.k.S.k.K.kkS..k..k.S.s.";
        for (i = 0; i < 32; i++) {
            if (pat[i] == 'K' || pat[i] == 'k') add_kick(b, i * step, n);
            if (pat[i] == 'S') add_snare(b, i * step, n, 1.0);
            if (pat[i] == 's') add_snare(b, i * step, n, 0.5);
            add_hat(b, i * step, n, (i & 1) ? 0.6 : 1.0);
        }
        setins(a, 1, "break 170", to16(b, n), n, 0, 0);
        st_chop_equal(a, &s->instr[1], 16);
    }
    { int n = SR / 2; float *b = calloc(n, sizeof(float)); add_kick(b, 0, n); setins(a, 2, "kick", to16(b, n), n, 0, 0); }
    { int n = SR / 3; float *b = calloc(n, sizeof(float)); add_snare(b, 0, n, 1); setins(a, 3, "snare", to16(b, n), n, 0, 0); }
    { /* sub: one second of 55 Hz sine, loop a whole number of cycles */
        int n = SR; float *b = calloc(n, sizeof(float)); int cyc = (int)(SR / 55.0 * 50);
        for (i = 0; i < n; i++) b[i] = (float)sin(2 * M_PI * 55.0 * i / SR);
        setins(a, 4, "sub", to16(b, n), n, 0, cyc); s->instr[4].transpose = 0; }
    { /* reese: two detuned saws */
        int n = SR * 2; float *b = calloc(n, sizeof(float));
        for (i = 0; i < n; i++) { double t = (double)i / SR; b[i] = (float)(fmod(t * 55.0, 1.0) + fmod(t * 55.6, 1.0) - 1.0); }
        setins(a, 5, "reese", to16(b, n), n, SR / 2, SR); }
    { /* pad: minor chord */
        int n = SR * 2; float *b = calloc(n, sizeof(float));
        for (i = 0; i < n; i++) { double t = (double)i / SR; b[i] = (float)(sin(2*M_PI*220*t) + sin(2*M_PI*261.6*t) + sin(2*M_PI*329.6*t) + 0.5*sin(2*M_PI*440.5*t)); }
        setins(a, 6, "pad Am", to16(b, n), n, SR / 4, SR); s->instr[6].volume = 40; }
    /* block 0: intro (pad + hats from the break) */
    strcpy(s->blocks[0].name, "intro");
    cell(s, 0, 0, 3, NOTE_C3, 6, 0, 0);
    for (l = 0; l < 64; l += 4) cell(s, 0, l + 2, 1, NOTE_C3 + 3, 1, 0x0C, 0x18);
    cell(s, 0, 32, 3, NOTE_C3 - 2, 6, 0, 0);
    /* block 1: drop A, break played in order, sub line */
    { int b = block_new(a, s, 64); strcpy(s->blocks[b].name, "drop A");
      for (l = 0; l < 64; l += 2) cell(s, b, l, 0, NOTE_C3 + (l / 2) % 16, 1, 0, 0);
      cell(s, b, 0, 2, NOTE_C3 - 24, 4, 0, 0); cell(s, b, 24, 2, NOTE_C3 - 26, 4, 0x03, 0x08);
      cell(s, b, 32, 2, NOTE_C3 - 24, 4, 0, 0); cell(s, b, 56, 2, NOTE_C3 - 21, 4, 0, 0);
      cell(s, b, 0, 4, NOTE_C3 - 12, 5, 0x0C, 0x20); cell(s, b, 32, 4, NOTE_C3 - 14, 5, 0x0C, 0x20);
      /* block 2: linked variation of drop A with a re-programmed break on track 1 only */
      { int v = block_make_variation(a, s, b); static const int rearr[16] = { 0,1,2,3, 4,5,4,5, 8,9,12,13, 14,10,6,15 };
        strcpy(s->blocks[v].name, "drop B");
        for (l = 0; l < 64; l += 2) cell(s, v, l, 0, NOTE_C3 + rearr[(l / 2) % 16], 1, (l % 16 == 14) ? 0x1F : 0, (l % 16 == 14) ? 0x02 : 0);
        /* sections and timeline */
        strcpy(s->sections[0].name, "Intro");
        { int d = section_new(s, "Drop"), br = section_new(s, "Breakdown");
          s->sections[d].blocks[0] = b; s->sections[d].blocks[1] = v; s->sections[d].count = 2;
          s->sections[br].blocks[0] = 0; s->sections[br].count = 1;
          s->orderLen = 0;
          s->order[s->orderLen++] = 0; s->order[s->orderLen++] = d; s->order[s->orderLen++] = d;
          s->order[s->orderLen++] = br; s->order[s->orderLen++] = d; } } }
    if (song_save(a, argv[1]) != 1) { puts("save failed"); return 1; }
    a->ui.ordCur = 0;
    player_play(a, PLAY_SONG, 0);
    {   /* render a preview to check it sounds sane */
        extern void host_wav(App *, const char *, int); host_wav(a, argv[2], 30); }
    printf("demo song: %d timeline entries, %d lines\n", s->orderLen, song_total_lines(s));
    return 0;
}
