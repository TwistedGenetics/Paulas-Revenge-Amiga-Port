/* Arrangement: timeline of named sections, section contents, section library. */
#include "ui_int.h"

#define BOX_W 10
#define VIS_BOXES 8
#define LIST_TOP 10
#define LIST_ROWS 12

static int cur_section(App *a)
{
    Song *s = a->song;
    if (a->ui.ordCur < s->orderLen) return s->order[a->ui.ordCur];
    return -1;
}

static void clamp_cursors(App *a)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int sec;
    if (u->ordCur >= s->orderLen) u->ordCur = s->orderLen - 1;
    if (u->ordCur < 0) u->ordCur = 0;
    sec = cur_section(a);
    if (sec >= 0) {
        int cnt = s->sections[sec].count;
        if (u->secSlotCur >= cnt) u->secSlotCur = cnt - 1;
    }
    if (u->secSlotCur < 0) u->secSlotCur = 0;
    if (u->secListCur < 0) u->secListCur = 0;
    if (u->secListCur >= MAX_SECTIONS) u->secListCur = MAX_SECTIONS - 1;
}

static int next_used_section(const Song *s, int from, int dir)
{
    int i, k = from;
    for (i = 0; i < MAX_SECTIONS; i++) {
        k += dir;
        if (k < 0) k = MAX_SECTIONS - 1;
        if (k >= MAX_SECTIONS) k = 0;
        if (s->sections[k].used) return k;
    }
    return from;
}

void arrange_draw(App *a)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    Player *p = &a->pl;
    char *t = a->tmp;
    int i, n, first, sec, total, playOrd = p->mode == PLAY_SONG ? p->curOrder : -1;

    clamp_cursors(a);
    draw_header(a, "ARRANGE");
    total = song_total_lines(s);
    n = 0;
    memcpy(t, "Song \"", 6); n = 6;
    memcpy(t + n, s->name, strlen(s->name)); n += (int)strlen(s->name);
    memcpy(t + n, "\"  Timeline ", 12); n += 12; n += fmt_dec(t + n, s->orderLen, 1);
    memcpy(t + n, " sections, ", 11); n += 11; n += fmt_dec(t + n, (u32)total, 1);
    memcpy(t + n, " lines", 6); n += 6;
    t[n] = 0;
    gfx_text(a, 0, 1, t, 0);
    draw_fkeys(a, "TAB pane  I insert  DEL remove  Shift+arrows reorder  +/- change  R rename");

    /* --- timeline boxes --- */
    gfx_text(a, 0, 3, "TIMELINE", 0);
    if (u->arrFocus == 0) gfx_fill_row(a, 3, 0, 7, 2, 1);
    first = u->ordCur - VIS_BOXES / 2;
    if (first > s->orderLen - VIS_BOXES) first = s->orderLen - VIS_BOXES;
    if (first < 0) first = 0;
    for (i = 0; i < VIS_BOXES && first + i < s->orderLen; i++) {
        int o = first + i, x = i * BOX_W, sc = s->order[o];
        n = fmt_dec(t, (u32)o, 2);
        t[n++] = ' ';
        str_ncpy(t + n, s->sections[sc].name, BOX_W - n);
        gfx_textn(a, x, 4, t, BOX_W - 1, 0);
        n = fmt_dec(t, (u32)section_lines(s, sc), 1);
        memcpy(t + n, " ln", 4);
        gfx_textn(a, x, 5, t, BOX_W - 1, 0);
        gfx_fill_row(a, 4, x, x + BOX_W - 2, 1, 1);
        gfx_fill_row(a, 5, x, x + BOX_W - 2, 1, 1);
        if (o == u->ordCur) { gfx_fill_row(a, 4, x, x + BOX_W - 2, 2, 1); gfx_fill_row(a, 5, x, x + BOX_W - 2, 2, 1); }
        if (o == playOrd) gfx_text(a, x + BOX_W - 2, 4, ">", 0);
    }
    if (first > 0) gfx_text(a, 79, 3, "<", 0);
    if (first + VIS_BOXES < s->orderLen) gfx_text(a, 79, 6, ">", 0);

    /* --- proportional overview bar (pixels, rows 6-7) --- */
    if (total > 0) {
        int acc = 0, y0 = 6 * FONT_H + 2, y1 = 8 * FONT_H - 3;
        gfx_hline(a, 0, SCREEN_W - 1, y0, 0);
        gfx_hline(a, 0, SCREEN_W - 1, y1, 0);
        for (i = 0; i < s->orderLen; i++) {
            int len = section_lines(s, s->order[i]);
            int x0 = (int)((u32)acc * (SCREEN_W - 1) / (u32)total);
            int x1 = (int)((u32)(acc + len) * (SCREEN_W - 1) / (u32)total);
            gfx_vline(a, x0, y0, y1, 0);
            if (i == u->ordCur) gfx_rect_fill(a, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 2, 1);
            else if (i & 1) gfx_rect_fill(a, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 1, 1);
            if (i == playOrd && p->curBlock >= 0) {
                /* play-head inside the section */
                const Section *sc = &s->sections[s->order[i]];
                int j, inner = 0;
                for (j = 0; j < sc->count && j < p->secSlot; j++) inner += s->blocks[sc->blocks[j]].lines;
                inner += p->curLine;
                {
                    int px = (int)((u32)(acc + inner) * (SCREEN_W - 1) / (u32)total);
                    gfx_vline(a, px, y0 - 2, y1 + 2, 0);
                    gfx_vline(a, px + 1, y0 - 2, y1 + 2, 0);
                }
            }
            acc += len;
        }
        gfx_vline(a, SCREEN_W - 1, y0, y1, 0);
    }

    /* --- current section contents --- */
    sec = cur_section(a);
    if (sec >= 0) {
        const Section *sc = &s->sections[sec];
        n = 0;
        memcpy(t, "SECTION \"", 9); n = 9;
        memcpy(t + n, sc->name, strlen(sc->name)); n += (int)strlen(sc->name);
        memcpy(t + n, "\" BLOCKS", 9); n += 9;
        gfx_text(a, 0, 9, t, 0);
        if (u->arrFocus == 1) gfx_fill_row(a, 9, 0, n - 1, 2, 1);
        first = u->secSlotCur - LIST_ROWS / 2;
        if (first > sc->count - LIST_ROWS) first = sc->count - LIST_ROWS;
        if (first < 0) first = 0;
        for (i = 0; i < LIST_ROWS && first + i < sc->count; i++) {
            int j = first + i, b = sc->blocks[j];
            const Block *bl = &s->blocks[b];
            n = fmt_dec(t, (u32)j + 1, 2);
            memcpy(t + n, " Blk ", 5); n += 5; n += fmt_hex(t + n, (u32)b, 2);
            t[n++] = ' ';
            str_ncpy(t + n, bl->name, 13); n += (int)strlen(t + n);
            while (n < 22) t[n++] = ' ';
            n += fmt_dec(t + n, bl->lines, 3);
            if (bl->parent >= 0) { memcpy(t + n, " var/", 5); n += 5; n += fmt_hex(t + n, (u32)bl->parent, 2); }
            t[n] = 0;
            gfx_text(a, 0, LIST_TOP + i, t, 0);
            if (j == u->secSlotCur) gfx_fill_row(a, LIST_TOP + i, 0, 38, u->arrFocus == 1 ? 2 : 1, 1);
            if (p->mode == PLAY_SONG && p->curOrder == u->ordCur && p->secSlot == j) gfx_text(a, 38, LIST_TOP + i, "<", 0);
        }
        if (!sc->count) gfx_text(a, 0, LIST_TOP, "(empty: press A to add the current block)", 0);
    } else {
        gfx_text(a, 0, 9, "Timeline is empty: TAB to the library and press I", 0);
    }

    /* --- library --- */
    gfx_text(a, 42, 9, "SECTION LIBRARY", 0);
    if (u->arrFocus == 2) gfx_fill_row(a, 9, 42, 56, 2, 1);
    {
        int row = 0, k;
        int start = 0, cnt = 0;
        /* scroll so the cursor is visible */
        for (k = 0; k < u->secListCur; k++) if (s->sections[k].used) cnt++;
        if (cnt >= LIST_ROWS) start = cnt - LIST_ROWS + 1;
        cnt = 0;
        for (k = 0; k < MAX_SECTIONS && row < LIST_ROWS; k++) {
            const Section *sc = &s->sections[k];
            if (!sc->used) continue;
            if (cnt++ < start) continue;
            n = fmt_hex(t, (u32)k, 2);
            t[n++] = ' ';
            str_ncpy(t + n, sc->name, 16); n += (int)strlen(t + n);
            while (n < 19) t[n++] = ' ';
            n += fmt_rdec(t + n, sc->count, 2);
            memcpy(t + n, " blk", 4); n += 4;
            n += fmt_rdec(t + n, (u32)section_lines(s, k), 5);
            memcpy(t + n, " ln", 4);
            gfx_text(a, 42, LIST_TOP + row, t, 0);
            if (k == u->secListCur) gfx_fill_row(a, LIST_TOP + row, 42, 79, u->arrFocus == 2 ? 2 : 1, 1);
            row++;
        }
    }
    gfx_text(a, 42, 22, "N new  C copy  V copy as variations", 0);
}

void arrange_key(App *a, const Event *e)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int code = e->code, shift = (e->qual & Q_SHIFT) != 0;
    int sec = cur_section(a);
    int plus = (code == K_EQUALS), minus = (code == K_MINUS);

    if (!s->sections[u->secListCur].used) u->secListCur = next_used_section(s, u->secListCur, 1);

    if (code == K_TAB) { u->arrFocus = (u->arrFocus + (shift ? 2 : 1)) % 3; return; }
    if (code == K_R && !(e->qual & Q_CTRL)) {
        int target = u->arrFocus == 2 ? u->secListCur : sec;
        if (target < 0) return;
        u->secListCur = target;
        ui_prompt(a, PR_SEC_NAME, "Section name:", s->sections[target].name);
        return;
    }

    if (u->arrFocus == 0) {          /* ---- timeline ---- */
        switch (code) {
        case K_LEFT:
            if (shift && u->ordCur > 0) { u8 tmp = s->order[u->ordCur - 1]; s->order[u->ordCur - 1] = s->order[u->ordCur]; s->order[u->ordCur] = tmp; u->dirty = 1; }
            if (u->ordCur > 0) u->ordCur--;
            u->secSlotCur = 0; return;
        case K_RIGHT:
            if (shift && u->ordCur < s->orderLen - 1) { u8 tmp = s->order[u->ordCur + 1]; s->order[u->ordCur + 1] = s->order[u->ordCur]; s->order[u->ordCur] = tmp; u->dirty = 1; }
            if (u->ordCur < s->orderLen - 1) u->ordCur++;
            u->secSlotCur = 0; return;
        case K_DOWN: u->arrFocus = 1; return;
        case K_I:
            if (s->orderLen >= MAX_ORDER) return;
            {
                int at = s->orderLen ? u->ordCur + 1 : 0;
                int what = sec >= 0 ? sec : u->secListCur;
                memmove(&s->order[at + 1], &s->order[at], (unsigned long)(s->orderLen - at));
                s->order[at] = (u8)what; s->orderLen++;
                u->ordCur = at; u->dirty = 1;
            }
            return;
        case K_DEL:
            if (!s->orderLen) return;
            player_stop(a);
            memmove(&s->order[u->ordCur], &s->order[u->ordCur + 1], (unsigned long)(s->orderLen - u->ordCur - 1));
            s->orderLen--; u->dirty = 1;
            clamp_cursors(a);
            return;
        }
        if ((plus || minus) && sec >= 0) {
            s->order[u->ordCur] = (u8)next_used_section(s, sec, plus ? 1 : -1);
            u->dirty = 1;
        }
        return;
    }

    if (u->arrFocus == 1) {          /* ---- section contents ---- */
        Section *sc;
        if (sec < 0) { if (code == K_UP) u->arrFocus = 0; return; }
        sc = &s->sections[sec];
        switch (code) {
        case K_UP:
            if (u->secSlotCur == 0) { u->arrFocus = 0; return; }
            if (shift) { u8 tmp = sc->blocks[u->secSlotCur - 1]; sc->blocks[u->secSlotCur - 1] = sc->blocks[u->secSlotCur]; sc->blocks[u->secSlotCur] = tmp; u->dirty = 1; }
            u->secSlotCur--; return;
        case K_DOWN:
            if (u->secSlotCur < sc->count - 1) {
                if (shift) { u8 tmp = sc->blocks[u->secSlotCur + 1]; sc->blocks[u->secSlotCur + 1] = sc->blocks[u->secSlotCur]; sc->blocks[u->secSlotCur] = tmp; u->dirty = 1; }
                u->secSlotCur++;
            }
            return;
        case K_A: case K_N: {
            int b = u->block, at;
            if (sc->count >= MAX_SEC_BLOCKS) { ui_status(a, "Section is full."); return; }
            if (code == K_N) {
                b = block_new(a, s, 64);
                if (b < 0) { ui_status(a, "No free blocks."); return; }
            }
            at = sc->count ? u->secSlotCur + 1 : 0;
            memmove(&sc->blocks[at + 1], &sc->blocks[at], (unsigned long)(sc->count - at));
            sc->blocks[at] = (u8)b; sc->count++;
            u->secSlotCur = at; u->dirty = 1;
            return;
        }
        case K_DEL:
            if (!sc->count) return;
            player_stop(a);
            memmove(&sc->blocks[u->secSlotCur], &sc->blocks[u->secSlotCur + 1], (unsigned long)(sc->count - u->secSlotCur - 1));
            sc->count--; u->dirty = 1;
            clamp_cursors(a);
            return;
        case K_E:
            if (sc->count) { u->block = sc->blocks[u->secSlotCur]; u->line = 0; goto_screen(a, SCR_BLOCK); }
            return;
        case K_L:
            if (sc->count) {
                int nb = block_make_variation(a, s, sc->blocks[u->secSlotCur]);
                if (nb < 0) { ui_status(a, "No free blocks."); return; }
                sc->blocks[u->secSlotCur] = (u8)nb; u->dirty = 1;
                ui_status(a, "Slot now uses a linked variation of its block.");
            }
            return;
        }
        if ((plus || minus) && sc->count) {
            int b = sc->blocks[u->secSlotCur], i;
            for (i = 0; i < MAX_BLOCKS; i++) {
                b += plus ? 1 : -1;
                if (b < 0) b = MAX_BLOCKS - 1;
                if (b >= MAX_BLOCKS) b = 0;
                if (s->blocks[b].used) break;
            }
            sc->blocks[u->secSlotCur] = (u8)b; u->dirty = 1;
        }
        return;
    }

    /* ---- library ---- */
    switch (code) {
    case K_UP: u->secListCur = next_used_section(s, u->secListCur, -1); return;
    case K_DOWN: u->secListCur = next_used_section(s, u->secListCur, 1); return;
    case K_N: {
        int n = section_new(s, "Section");
        if (n < 0) { ui_status(a, "No free sections."); return; }
        u->secListCur = n; u->dirty = 1;
        ui_prompt(a, PR_SEC_NAME, "Section name:", "");
        return;
    }
    case K_C: case K_V: {
        int n = section_clone(a, s, u->secListCur, code == K_V);
        if (n < 0) { ui_status(a, "No room for a copy."); return; }
        u->secListCur = n; u->dirty = 1;
        ui_status(a, code == K_V ? "Section copied with linked variation blocks." : "Section copied (same blocks).");
        return;
    }
    case K_I: {
        int at = s->orderLen ? u->ordCur + 1 : 0;
        if (s->orderLen >= MAX_ORDER) return;
        memmove(&s->order[at + 1], &s->order[at], (unsigned long)(s->orderLen - at));
        s->order[at] = (u8)u->secListCur; s->orderLen++;
        u->ordCur = at; u->dirty = 1;
        ui_status(a, "Section placed on the timeline.");
        return;
    }
    case K_DEL:
        ui_prompt(a, PR_DEL_SEC, "Delete section (and remove it from the timeline)? (y/n)", "");
        return;
    }
}
