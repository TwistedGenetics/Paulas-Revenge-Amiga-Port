/* Block (pattern) editor, OctaMED style. */
#include "ui_int.h"

#define TRK_W 12
#define VIS_TRACKS 6
#define PAT_TOP 4
#define PAT_ROWS 19
#define CUR_ROW (PAT_TOP + PAT_ROWS / 2)

static const u8 subx[7] = { 0, 4, 5, 7, 8, 9, 10 };

void block_ensure_valid(App *a)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int i;
    if (u->block < 0 || u->block >= MAX_BLOCKS || !s->blocks[u->block].used) {
        for (i = 0; i < MAX_BLOCKS; i++) if (s->blocks[i].used) { u->block = i; break; }
    }
    if (u->line >= s->blocks[u->block].lines) u->line = s->blocks[u->block].lines - 1;
    if (u->line < 0) u->line = 0;
    if (u->track >= s->tracks) u->track = s->tracks - 1;
    if (u->track < 0) u->track = 0;
    if (u->trackOfs > u->track) u->trackOfs = u->track;
    if (u->track >= u->trackOfs + VIS_TRACKS) u->trackOfs = u->track - VIS_TRACKS + 1;
}

static void fmt_cell(const Cell *c, char *o)
{
    note_name(c->note, o);
    o[3] = ' ';
    if (c->instr) fmt_hex(o + 4, c->instr, 2); else { o[4] = '-'; o[5] = '-'; }
    o[6] = ' ';
    if (c->cmd || c->param) { fmt_hex(o + 7, c->cmd, 2); fmt_hex(o + 9, c->param, 2); }
    else { o[7] = o[8] = o[9] = o[10] = '-'; }
    o[11] = 0;
}

static void count_own(const Song *s, int b, int *own)
{
    int t;
    *own = 0;
    for (t = 0; t < s->tracks; t++) if (!block_track_linked(s, b, t)) (*own)++;
}

void block_draw(App *a)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    Player *p = &a->pl;
    Block *bl;
    char *t = a->tmp;
    int r, tr, n, following = 0, playLine = -1;

    if (p->mode != PLAY_STOP && !u->edit) {
        int pb = p->curBlock;
        if (pb >= 0 && pb < MAX_BLOCKS && s->blocks[pb].used) { u->block = pb; u->line = p->curLine; following = 1; }
    }
    block_ensure_valid(a);
    bl = &s->blocks[u->block];
    if (p->mode != PLAY_STOP && p->curBlock == u->block) playLine = p->curLine;

    draw_header(a, "BLOCK EDITOR");
    /* info row */
    n = 0;
    memcpy(t, "Block ", 6); n = 6; n += fmt_hex(t + n, (u32)u->block, 2);
    t[n++] = ' '; t[n++] = '"';
    memcpy(t + n, bl->name, strlen(bl->name)); n += (int)strlen(bl->name);
    t[n++] = '"';
    memcpy(t + n, "  Lines ", 8); n += 8; n += fmt_dec(t + n, bl->lines, 3);
    memcpy(t + n, "  Step ", 7); n += 7; n += fmt_dec(t + n, (u32)u->step, 1);
    if (bl->parent >= 0) {
        int own;
        count_own(s, u->block, &own);
        memcpy(t + n, "  VARIATION of ", 15); n += 15; n += fmt_hex(t + n, (u32)bl->parent, 2);
        memcpy(t + n, " (", 2); n += 2; n += fmt_dec(t + n, (u32)own, 1);
        memcpy(t + n, " own)", 5); n += 5;
    }
    t[n] = 0;
    gfx_text(a, 0, 1, t, 0);
    draw_fkeys(a, following ? "Following playback. SPACE stops." :
               "SPC edit  RET play  Shift+RET loop  Ctrl+N new  Ctrl+L variation  Ctrl+R relink");

    /* track headers */
    gfx_text(a, 0, 3, "LINE", 0);
    for (tr = 0; tr < VIS_TRACKS && u->trackOfs + tr < s->tracks; tr++) {
        int ti = u->trackOfs + tr, x = 4 + tr * TRK_W;
        n = 0;
        memcpy(t, "Trk", 3); n = 3; n += fmt_dec(t + n, (u32)ti + 1, 2);
        if (block_track_linked(s, u->block, ti)) { memcpy(t + n, " LINK", 5); n += 5; }
        else if (bl->parent >= 0) { memcpy(t + n, " OWN ", 5); n += 5; }
        if (s->trackMute[ti]) { memcpy(t + n, " M", 2); n += 2; }
        t[n] = 0;
        gfx_text(a, x, 3, t, 0);
        if (s->trackMute[ti]) gfx_fill_row(a, 3, x, x + TRK_W - 2, 2, 1);
    }

    for (r = 0; r < PAT_ROWS; r++) {
        int line = u->line + r - (CUR_ROW - PAT_TOP);
        int row = PAT_TOP + r;
        if (line < 0 || line >= bl->lines) continue;
        fmt_dec(t, (u32)line, 3);
        gfx_text(a, 0, row, t, 0);
        if (line == playLine) gfx_fill_row(a, row, 0, COLS - 1, 1, 1);
        for (tr = 0; tr < VIS_TRACKS && u->trackOfs + tr < s->tracks; tr++) {
            int ti = u->trackOfs + tr, x = 4 + tr * TRK_W;
            fmt_cell(block_cell(s, u->block, line, ti), t);
            gfx_text(a, x, row, t, 0);
            gfx_text(a, x + 11, row, "|", 0);
            if (block_track_linked(s, u->block, ti) && line != playLine) gfx_fill_row(a, row, x, x + 10, 1, 1);
        }
        if ((line & 3) == 0 && line != playLine) gfx_fill_row(a, row, 0, 2, 1, 1);
    }
    /* cursor */
    gfx_fill_row(a, CUR_ROW, 0, 3, 2, 1);
    {
        int x = 4 + (u->track - u->trackOfs) * TRK_W + subx[u->col];
        gfx_fill_row(a, CUR_ROW, x, x + (u->col == 0 ? 2 : 0), 2, 1);
        if (u->edit) gfx_fill_row(a, CUR_ROW, 0, COLS - 1, 1, 1);
    }
}

static void jam(App *a, int note)
{
    player_preview(a, a->ui.instr, note, 0, 0);
}

static void move_line(App *a, int d)
{
    Ui *u = &a->ui;
    int lines = a->song->blocks[u->block].lines;
    u->line += d;
    while (u->line < 0) u->line += lines;
    while (u->line >= lines) u->line -= lines;
}

static void copy_region(App *a, int whole)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int l, t, lines = s->blocks[u->block].lines;
    int t0 = whole ? 0 : u->track, nt = whole ? s->tracks : 1;
    for (l = 0; l < lines; l++)
        for (t = 0; t < nt; t++)
            u->clip[l * MAX_TRACKS + t] = *block_cell(s, u->block, l, t0 + t);
    u->clipLines = lines; u->clipTracks = nt;
}

static void clear_region(App *a, int whole)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int l, t, lines = s->blocks[u->block].lines;
    int t0 = whole ? 0 : u->track, nt = whole ? s->tracks : 1;
    for (t = 0; t < nt; t++)
        for (l = 0; l < lines; l++) memset(block_cell_edit(s, u->block, l, t0 + t), 0, sizeof(Cell));
}

static void paste_region(App *a)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int l, t, lines = s->blocks[u->block].lines;
    int t0 = u->clipTracks > 1 ? 0 : u->track;
    if (!u->clipLines) { ui_status(a, "Clipboard is empty."); return; }
    for (l = 0; l < u->clipLines && u->line + l < lines; l++)
        for (t = 0; t < u->clipTracks && t0 + t < s->tracks; t++)
            *block_cell_edit(s, u->block, u->line + l, t0 + t) = u->clip[l * MAX_TRACKS + t];
    u->dirty = 1;
}

static void next_block(App *a, int dir)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int b = u->block, i;
    for (i = 0; i < MAX_BLOCKS; i++) {
        b += dir;
        if (b < 0) b = MAX_BLOCKS - 1;
        if (b >= MAX_BLOCKS) b = 0;
        if (s->blocks[b].used) { u->block = b; break; }
    }
    block_ensure_valid(a);
}

void block_key(App *a, const Event *e)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int code = e->code;
    int shift = (e->qual & Q_SHIFT) != 0, ctrl = (e->qual & Q_CTRL) != 0, alt = (e->qual & Q_ALT) != 0;
    int lines = s->blocks[u->block].lines;
    char *t = a->tmp;

    if (ctrl) {
        int nb;
        switch (code) {
        case K_UP: next_block(a, -1); return;
        case K_DOWN: next_block(a, 1); return;
        case K_N:
            nb = block_new(a, s, lines);
            if (nb < 0) { ui_status(a, "No free blocks."); return; }
            u->block = nb; u->line = 0; u->dirty = 1;
            ui_status(a, "New empty block.");
            return;
        case K_D:
            nb = block_clone(a, s, u->block);
            if (nb < 0) { ui_status(a, "No free blocks."); return; }
            u->block = nb; u->dirty = 1;
            ui_status(a, "Block duplicated (independent copy).");
            return;
        case K_L:
            nb = block_make_variation(a, s, u->block);
            if (nb < 0) { ui_status(a, "No free blocks."); return; }
            u->block = nb; u->dirty = 1;
            ui_status(a, "Linked variation made. Edited tracks become OWN; the rest follow the original.");
            return;
        case K_R:
            if (s->blocks[u->block].parent < 0) { ui_status(a, "This block is not a variation."); return; }
            block_relink_track(s, u->block, u->track); u->dirty = 1;
            ui_status(a, "Track relinked to the original block.");
            return;
        case K_B: fmt_dec(t, (u32)lines, 1); ui_prompt(a, PR_BLOCK_LINES, "Block lines (1-256):", t); return;
        case K_E: ui_prompt(a, PR_BLOCK_NAME, "Block name:", s->blocks[u->block].name); return;
        case K_C: copy_region(a, shift); ui_status(a, shift ? "Block copied." : "Track copied."); return;
        case K_X: copy_region(a, shift); clear_region(a, shift); u->dirty = 1; ui_status(a, shift ? "Block cut." : "Track cut."); return;
        case K_V: paste_region(a); return;
        case K_M:
            if (shift) {
                int i, solo = 1;
                for (i = 0; i < s->tracks; i++) if (i != u->track && !s->trackMute[i]) solo = 0;
                for (i = 0; i < s->tracks; i++) s->trackMute[i] = solo ? 0 : (u8)(i != u->track);
            } else s->trackMute[u->track] = !s->trackMute[u->track];
            return;
        case K_DEL: ui_prompt(a, PR_DEL_BLOCK, "Delete this block? (y/n)", ""); return;
        }
        return;
    }
    if (alt) {
        int d = key_digit(code);
        if (d >= 0) { u->step = d; return; }
        return;
    }

    switch (code) {
    case K_UP: move_line(a, shift ? -16 : -1); return;
    case K_DOWN: move_line(a, shift ? 16 : 1); return;
    case K_LEFT:
        if (u->col > 0) u->col--;
        else if (u->track > 0) { u->track--; u->col = 6; }
        block_ensure_valid(a); return;
    case K_RIGHT:
        if (u->col < 6) u->col++;
        else if (u->track < s->tracks - 1) { u->track++; u->col = 0; }
        block_ensure_valid(a); return;
    case K_TAB:
        if (shift) { if (u->track > 0) u->track--; }
        else if (u->track < s->tracks - 1) u->track++;
        u->col = 0; block_ensure_valid(a); return;
    case K_SPACE:
        u->edit = !u->edit; return;
    }

    if (code == K_LBRACK && !u->edit) { if (u->instr > 1) u->instr--; return; }
    if (code == K_RBRACK && !u->edit) { if (u->instr < MAX_INSTR - 1) u->instr++; return; }

    if (!u->edit) {
        int off = key_note_offset(code);
        if (off >= 0) { int note = (u->octave - 1) * 12 + off + 1; if (note <= NOTE_MAX) jam(a, note); }
        return;
    }

    /* ---- edit mode ---- */
    if (code == K_DEL) {
        Cell *c = block_cell_edit(s, u->block, u->line, u->track);
        if (shift) memset(c, 0, sizeof(Cell));
        else if (u->col == 0) { c->note = 0; c->instr = 0; }
        else if (u->col <= 2) c->instr = 0;
        else { c->cmd = 0; c->param = 0; }
        u->dirty = 1;
        move_line(a, u->step);
        return;
    }
    if (code == K_BACKSPACE) {
        int l;
        if (shift) {   /* insert blank line */
            for (l = lines - 1; l > u->line; l--)
                *block_cell_edit(s, u->block, l, u->track) = *block_cell(s, u->block, l - 1, u->track);
            memset(block_cell_edit(s, u->block, u->line, u->track), 0, sizeof(Cell));
        } else {       /* delete line, pull up */
            for (l = u->line; l < lines - 1; l++)
                *block_cell_edit(s, u->block, l, u->track) = *block_cell(s, u->block, l + 1, u->track);
            memset(block_cell_edit(s, u->block, lines - 1, u->track), 0, sizeof(Cell));
        }
        u->dirty = 1;
        return;
    }
    if (u->col == 0) {
        int off = key_note_offset(code);
        if (code == K_1 || code == K_BACKTICK) {
            Cell *c = block_cell_edit(s, u->block, u->line, u->track);
            c->note = NOTE_OFF; c->instr = 0;
            u->dirty = 1; move_line(a, u->step);
            return;
        }
        if (off >= 0) {
            int note = (u->octave - 1) * 12 + off + 1;
            Cell *c;
            if (note > NOTE_MAX) return;
            c = block_cell_edit(s, u->block, u->line, u->track);
            c->note = (u8)note; c->instr = (u8)u->instr;
            jam(a, note);
            u->dirty = 1; move_line(a, u->step);
        }
        return;
    }
    {
        int h = key_hex(code);
        Cell *c;
        if (h < 0) return;
        c = block_cell_edit(s, u->block, u->line, u->track);
        switch (u->col) {
        case 1: c->instr = (u8)(((h << 4) | (c->instr & 15)) & 0x3F); break;
        case 2: c->instr = (u8)((c->instr & 0xF0) | h); if (c->instr >= MAX_INSTR) c->instr = MAX_INSTR - 1; break;
        case 3: c->cmd = (u8)((h << 4) | (c->cmd & 15)); break;
        case 4: c->cmd = (u8)((c->cmd & 0xF0) | h); break;
        case 5: c->param = (u8)((h << 4) | (c->param & 15)); break;
        case 6: c->param = (u8)((c->param & 0xF0) | h); break;
        }
        u->dirty = 1;
        move_line(a, u->step);
    }
}
