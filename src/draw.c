/* CPU drawing into the three 640x256 bitplanes.
   plane 0 = ink (black), plane 1 = soft highlight, plane 2 = cursor/selection. */
#include "app.h"
#include "font.inc"

void gfx_clear(App *a)
{
    int p;
    for (p = 0; p < 3; p++) memset(a->plane[p], 0, ROWBYTES * SCREEN_H);
}

void gfx_clear_rows(App *a, int r0, int r1)
{
    int p;
    if (r0 < 0) r0 = 0;
    if (r1 >= ROWS) r1 = ROWS - 1;
    if (r1 < r0) return;
    for (p = 0; p < 3; p++)
        memset(a->plane[p] + r0 * FONT_H * ROWBYTES, 0, (unsigned long)((r1 - r0 + 1) * FONT_H * ROWBYTES));
}

void gfx_textn(App *a, int col, int row, const char *s, int n, int inv)
{
    u8 *base;
    int i, y;
    if (row < 0 || row >= ROWS) return;
    base = a->plane[0] + row * FONT_H * ROWBYTES;
    for (i = 0; i < n && s[i]; i++) {
        int c = (u8)s[i], x = col + i;
        const u8 *g;
        if (x < 0) continue;
        if (x >= COLS) break;
        if (c < 32 || c > 127) c = '?';
        g = font8x10 + (c - 32) * FONT_H;
        for (y = 0; y < FONT_H; y++) base[y * ROWBYTES + x] = inv ? (u8)~g[y] : g[y];
    }
}

void gfx_text(App *a, int col, int row, const char *s, int inv)
{
    gfx_textn(a, col, row, s, COLS, inv);
}

void gfx_fill_row(App *a, int row, int c0, int c1, int plane, int on)
{
    u8 *base;
    int y;
    if (row < 0 || row >= ROWS) return;
    if (c0 < 0) c0 = 0;
    if (c1 >= COLS) c1 = COLS - 1;
    if (c1 < c0) return;
    base = a->plane[plane] + row * FONT_H * ROWBYTES;
    for (y = 0; y < FONT_H; y++) memset(base + y * ROWBYTES + c0, on ? 0xFF : 0, (unsigned long)(c1 - c0 + 1));
}

void gfx_pset(App *a, int x, int y, int plane)
{
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    a->plane[plane][y * ROWBYTES + (x >> 3)] |= (u8)(0x80 >> (x & 7));
}

void gfx_hline(App *a, int x0, int x1, int y, int plane)
{
    int x;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (x = x0; x <= x1; x++) gfx_pset(a, x, y, plane);
}

void gfx_vline(App *a, int x, int y0, int y1, int plane)
{
    int y;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (y = y0; y <= y1; y++) gfx_pset(a, x, y, plane);
}

void gfx_rect_fill(App *a, int x0, int y0, int x1, int y1, int plane, int on)
{
    int y, x;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
    if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
    for (y = y0; y <= y1; y++) {
        u8 *row = a->plane[plane] + y * ROWBYTES;
        for (x = x0; x <= x1; ) {
            if ((x & 7) == 0 && x + 7 <= x1) { row[x >> 3] = on ? 0xFF : 0; x += 8; continue; }
            if (on) row[x >> 3] |= (u8)(0x80 >> (x & 7));
            else row[x >> 3] &= (u8)~(0x80 >> (x & 7));
            x++;
        }
    }
}
