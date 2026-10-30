/* Replays a simulator script on the host build, mirroring the Amiga frame and
   audio timing, so its screenshots and audio can be diffed against the 68k
   binary running in sim/. usage: replay script outdir rootdir */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../src/app.h"

static char rootdir[512];
void *mem_alloc(App *a, u32 size, int chip) { (void)a; (void)chip; return calloc(1, size ? size : 4); }
void mem_free(App *a, void *p) { (void)a; free(p); }
void audio_lock(App *a) { (void)a; }
void audio_unlock(App *a) { (void)a; }
void plat_wait_frame(App *a) { (void)a; }
int plat_get_event(App *a, Event *e) { if (a->evTail == a->evHead) return 0; *e = a->ev[a->evTail]; a->evTail = (a->evTail + 1) % EVQ; return 1; }
static void hp(const char *n, char *out) { const char *c = strchr(n, ':'); snprintf(out, 700, "%s/%s", rootdir, c ? c + 1 : n); }
void *f_open(App *a, const char *name, int write) { char p[700]; (void)a; hp(name, p); return fopen(p, write ? "wb" : "rb"); }
s32 f_read(App *a, void *fh, void *buf, s32 len) { (void)a; return (s32)fread(buf, 1, (size_t)len, fh); }
s32 f_write(App *a, void *fh, const void *buf, s32 len) { (void)a; return (s32)fwrite(buf, 1, (size_t)len, fh); }
s32 f_size(App *a, void *fh) { long p, s; (void)a; p = ftell(fh); fseek(fh, 0, SEEK_END); s = ftell(fh); fseek(fh, p, SEEK_SET); return (s32)s; }
void f_close(App *a, void *fh) { (void)a; fclose(fh); }
static int lc_cmp(const char *x, const char *y) { for (;; x++, y++) { int c1 = (unsigned char)*x, c2 = (unsigned char)*y; if (c1 >= 'A' && c1 <= 'Z') c1 += 32; if (c2 >= 'A' && c2 <= 'Z') c2 += 32; if (c1 != c2 || !c1) return c1 - c2; } }
int dir_list(App *a, const char *path, DirEntry *out, int max)
{
    char p[700]; DIR *d; struct dirent *de; int n = 0, i, j; (void)a;
    hp(path, p); d = opendir(p); if (!d) return -1;
    while ((de = readdir(d)) && n < max) {
        char full[1400]; struct stat st;
        if (de->d_name[0] == '.') continue;
        snprintf(full, sizeof full, "%s/%s", p, de->d_name); stat(full, &st);
        str_ncpy(out[n].name, de->d_name, 32); out[n].isDir = S_ISDIR(st.st_mode); out[n].size = (s32)st.st_size; n++;
    }
    closedir(d);
    for (i = 1; i < n; i++) { DirEntry t = out[i]; j = i - 1;
        while (j >= 0 && ((out[j].isDir < t.isDir) || (out[j].isDir == t.isDir && lc_cmp(out[j].name, t.name) > 0))) { out[j + 1] = out[j]; j--; }
        out[j + 1] = t; }
    return n;
}
int sampler_peek(App *a, int chan) { (void)a; (void)chan; return 0; }
u32 sampler_record(App *a, s16 *buf, u32 frames, u32 rate, int chan) { (void)a; (void)buf; (void)rate; (void)chan; return frames; }

static void push(App *a, int type, int down, int code, int qual)
{
    Event *e = &a->ev[a->evHead];
    e->type = (u8)type; e->down = (u8)down; e->code = (u16)code; e->qual = (u16)qual; e->x = (s16)a->mouseX; e->y = (s16)a->mouseY;
    a->evHead = (a->evHead + 1) % EVQ;
}
static void dump(App *a, const char *dir, const char *name)
{
    static const unsigned char pal[8][3] = { {255,255,255},{0,0,0},{221,221,221},{0,0,0},{170,170,170},{0,0,0},{153,153,153},{0,0,0} };
    char p[700]; FILE *f; int x, y;
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name); f = fopen(p, "wb");
    fprintf(f, "P6 640 512 255\n");
    for (y = 0; y < 512; y++) for (x = 0; x < 640; x++) { int c = 0, k; for (k = 0; k < 3; k++) if (a->plane[k][(y / 2) * 80 + (x >> 3)] & (0x80 >> (x & 7))) c |= 1 << k; fwrite(pal[c], 1, 3, f); }
    fclose(f);
}

int main(int argc, char **argv)
{
    App *a = calloc(1, sizeof(App));
    FILE *sf = fopen(argv[1], "r"), *wav;
    char line[300], wp[700];
    struct act { int frame; char kind[16]; int code, qual, x, y; char arg[200]; } *acts = calloc(20000, sizeof(struct act));
    int nacts = 0, pos = 0, frame = 0, k, audioStarted = 0;
    double audPos = 0;
    s16 L[AUDIO_BLOCK], R[AUDIO_BLOCK];
    strcpy(rootdir, argv[3]);
    while (fgets(line, sizeof line, sf)) {
        char *p;
        if (line[0] == '#' || line[0] == '\n') continue;
        memset(&acts[nacts], 0, sizeof acts[0]);
        if (sscanf(line, "%d %15s", &acts[nacts].frame, acts[nacts].kind) < 2) continue;
        p = strchr(line, ' ') + 1; p = strchr(p, ' '); p = p ? p + 1 : (char *)"";
        if (!strcmp(acts[nacts].kind, "shot") || !strcmp(acts[nacts].kind, "type")) sscanf(p, "%199[^\n]", acts[nacts].arg);
        else if (!strcmp(acts[nacts].kind, "move")) sscanf(p, "%d %d", &acts[nacts].x, &acts[nacts].y);
        else if (!strcmp(acts[nacts].kind, "maxframes")) continue;
        else sscanf(p, "%x %x", (unsigned *)&acts[nacts].code, (unsigned *)&acts[nacts].qual);
        nacts++;
    }
    for (k = 0; k < 3; k++) a->plane[k] = calloc(ROWBYTES, SCREEN_H);
    if (!ui_init(a)) return 1;
    a->mouseX = 320; a->mouseY = 128;
    snprintf(wp, sizeof wp, "%s/audio.raw", argv[2]); wav = fopen(wp, "wb");
    while (a->ui.running && frame < 100000) {
        frame++;
        while (pos < nacts && acts[pos].frame <= frame) {
            typeof(acts[0]) *c = &acts[pos++];
            if (!strcmp(c->kind, "key")) push(a, EV_KEY, 1, c->code, c->qual);
            else if (!strcmp(c->kind, "move")) { int x = a->mouseX + c->x * 2, y = a->mouseY + c->y;
                if (x < 0) x = 0; if (x > 639) x = 639; if (y < 0) y = 0; if (y > 255) y = 255; a->mouseX = x; a->mouseY = y; push(a, EV_MOVE, 0, 0, 0); }
            else if (!strcmp(c->kind, "lmb")) push(a, EV_MOUSE, c->code, 0, 0);
            else if (!strcmp(c->kind, "shot")) dump(a, argv[2], c->arg);
            else if (!strcmp(c->kind, "type")) { const char *s = c->arg;
                static const char r1[] = "qwertyuiop", r2[] = "asdfghjkl", r3[] = "zxcvbnm", r0[] = "`1234567890";
                for (; *s; s++) { int ch = *s, code = -1, q = 0; const char *f;
                    if (ch >= 'A' && ch <= 'Z') { ch += 32; q = 1; }
                    if ((f = strchr(r1, ch))) code = 0x10 + (int)(f - r1); else if ((f = strchr(r2, ch))) code = 0x20 + (int)(f - r2);
                    else if ((f = strchr(r3, ch))) code = 0x31 + (int)(f - r3); else if ((f = strchr(r0, ch))) code = (int)(f - r0);
                    else if (ch == '.') code = 0x39; else if (ch == ' ') code = 0x40; else if (ch == '-') code = 0x0B;
                    if (code >= 0) push(a, EV_KEY, 1, code, q); } }
        }
        /* mirror sim audio timing: first VBL latches block 0 (silence), then one block per 1024 frames played */
        if (!audioStarted) { audioStarted = 1; memset(L, 0, sizeof L); memset(R, 0, sizeof R); }
        else {
            audPos += 28604.0 / 50.0;
            if (audPos >= 1024) {
                audPos -= 1024;
                /* sim writes the block that just finished, the vblank then fills the next one */
            }
        }
        if (frame == 1 || audPos < 28604.0 / 50.0) {
            player_render(a, L, R, AUDIO_BLOCK);
            for (k = 0; k < AUDIO_BLOCK; k++) { s16 l = (s16)(L[k] & ~3), r = (s16)(R[k] & ~3); fwrite(&l, 2, 1, wav); fwrite(&r, 2, 1, wav); }
        }
        ui_frame(a);
    }
    fclose(wav);
    printf("host replay: %d frames, running=%d\n", frame, a->ui.running);
    return 0;
}
