/* Host (Linux) platform for testing: headless, scripted events, PNG dumps. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include "../src/app.h"

void *mem_alloc(App *a, u32 size, int chip) { (void)a; (void)chip; return calloc(1, size ? size : 1); }
void mem_free(App *a, void *p) { (void)a; free(p); }
void audio_lock(App *a) { (void)a; }
void audio_unlock(App *a) { (void)a; }
void plat_wait_frame(App *a) { (void)a; }

int plat_get_event(App *a, Event *e)
{
    if (a->evTail == a->evHead) return 0;
    *e = a->ev[a->evTail];
    a->evTail = (a->evTail + 1) % EVQ;
    return 1;
}

void *f_open(App *a, const char *name, int write) { (void)a; return fopen(name[0] ? name : ".", write ? "wb" : "rb"); }
s32 f_read(App *a, void *fh, void *buf, s32 len) { (void)a; return (s32)fread(buf, 1, (size_t)len, (FILE *)fh); }
s32 f_write(App *a, void *fh, const void *buf, s32 len) { (void)a; return (s32)fwrite(buf, 1, (size_t)len, (FILE *)fh); }
s32 f_size(App *a, void *fh) { long p, s; (void)a; p = ftell((FILE *)fh); fseek((FILE *)fh, 0, SEEK_END); s = ftell((FILE *)fh); fseek((FILE *)fh, p, SEEK_SET); return (s32)s; }
void f_close(App *a, void *fh) { (void)a; fclose((FILE *)fh); }

int dir_list(App *a, const char *path, DirEntry *out, int max)
{
    DIR *d = opendir(path[0] ? path : ".");
    struct dirent *de;
    int n = 0;
    (void)a;
    if (!d) return -1;
    while ((de = readdir(d)) && n < max) {
        char full[600]; struct stat st;
        if (de->d_name[0] == '.') continue;
        snprintf(full, sizeof full, "%s/%s", path[0] ? path : ".", de->d_name);
        if (stat(full, &st)) continue;
        str_ncpy(out[n].name, de->d_name, 32);
        out[n].isDir = S_ISDIR(st.st_mode);
        out[n].size = (s32)st.st_size;
        n++;
    }
    closedir(d);
    return n;
}

int sampler_peek(App *a, int chan) { (void)chan; return (int)(12000 * sin(a->frame * 0.3 + (a->mouseX++ & 1023) * 0.05)); }
u32 sampler_record(App *a, s16 *buf, u32 frames, u32 rate, int chan)
{
    u32 i; (void)a; (void)chan;
    for (i = 0; i < frames; i++) buf[i] = (s16)(16000 * sin(2 * 3.14159265 * 220.0 * i / rate) * (1.0 - (double)i / frames));
    return frames;
}

/* ---- test helpers ---- */
void host_key(App *a, int code, int qual)
{
    Event *e = &a->ev[a->evHead];
    e->type = EV_KEY; e->down = 1; e->code = (u16)code; e->qual = (u16)qual;
    a->evHead = (a->evHead + 1) % EVQ;
}
void host_mouse(App *a, int type, int down, int x, int y)
{
    Event *e = &a->ev[a->evHead];
    a->mouseX = x; a->mouseY = y;
    e->type = (u8)type; e->down = (u8)down; e->code = 0; e->qual = 0; e->x = (s16)x; e->y = (s16)y;
    a->evHead = (a->evHead + 1) % EVQ;
}
void host_type(App *a, const char *s)
{
    static const char lo[] = "`1234567890-=\\";
    for (; *s; s++) {
        int c = *s, code = -1, q = 0, i;
        static const char row1[] = "qwertyuiop", row2[] = "asdfghjkl", row3[] = "zxcvbnm";
        if (c >= 'A' && c <= 'Z') { c += 32; q = 1; }
        for (i = 0; row1[i]; i++) if (row1[i] == c) code = 0x10 + i;
        for (i = 0; row2[i]; i++) if (row2[i] == c) code = 0x20 + i;
        for (i = 0; row3[i]; i++) if (row3[i] == c) code = 0x31 + i;
        for (i = 0; lo[i]; i++) if (lo[i] == c) code = i;
        if (c == ' ') code = 0x40;
        if (c == '.') code = 0x39;
        if (c == '/') code = 0x3A;
        if (c == ':') { code = 0x29; q = 1; }
        if (c == '_') { code = 0x0B; q = 1; }
        if (code >= 0) host_key(a, code, q);
    }
}

void host_dump(App *a, const char *path)
{
    static const unsigned char pal[8][3] = { {255,255,255},{0,0,0},{221,221,221},{0,0,0},{170,170,170},{0,0,0},{153,153,153},{0,0,0} };
    FILE *f = fopen(path, "wb");
    int x, y;
    fprintf(f, "P6 %d %d 255\n", SCREEN_W, SCREEN_H * 2);
    for (y = 0; y < SCREEN_H * 2; y++)
        for (x = 0; x < SCREEN_W; x++) {
            int yy = y / 2, bit = 0x80 >> (x & 7), c = 0, p;
            for (p = 0; p < 3; p++) if (a->plane[p][yy * ROWBYTES + (x >> 3)] & bit) c |= 1 << p;
            fwrite(pal[c], 1, 3, f);
        }
    fclose(f);
}

void host_wav(App *a, const char *path, int seconds)
{
    FILE *f = fopen(path, "wb");
    int n = OUT_RATE * seconds, i;
    s16 *L = malloc(n * 2), *R = malloc(n * 2);
    u32 dl = (u32)n * 4, rate = OUT_RATE;
    unsigned char h[44] = "RIFF";
    for (i = 0; i < n; i += AUDIO_BLOCK) player_render(a, L + i, R + i, n - i < AUDIO_BLOCK ? n - i : AUDIO_BLOCK);
    memcpy(h, "RIFF", 4); *(u32 *)(h + 4) = dl + 36; memcpy(h + 8, "WAVEfmt ", 8);
    *(u32 *)(h + 16) = 16; *(u16 *)(h + 20) = 1; *(u16 *)(h + 22) = 2; *(u32 *)(h + 24) = rate; *(u32 *)(h + 28) = rate * 4;
    *(u16 *)(h + 32) = 4; *(u16 *)(h + 34) = 16; memcpy(h + 36, "data", 4); *(u32 *)(h + 40) = dl;
    fwrite(h, 1, 44, f);
    for (i = 0; i < n; i++) { fwrite(&L[i], 2, 1, f); fwrite(&R[i], 2, 1, f); }
    fclose(f); free(L); free(R);
}
