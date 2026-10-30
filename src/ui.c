/* UI core: event dispatch, prompts, header, key maps. */
#include "ui_int.h"

/* raw key -> semitone offset from the base octave's C (two-row piano) */
static const s8 notemap[0x40] = {
/*00*/ -1,-1,13,15,-1,18,20,22,-1,25,27,-1,30,-1,-1,-1,
/*10*/ 12,14,16,17,19,21,23,24,26,28,29,31,-1,-1,-1,-1,
/*20*/ -1, 1, 3,-1, 6, 8,10,-1,13,15,-1,-1,-1,-1,-1,-1,
/*30*/ -1, 0, 2, 4, 5, 7, 9,11,12,14,16,-1,-1,-1,-1,-1
};

/* US keymap, unshifted / shifted, codes 0x00..0x3F */
static const char km_lo[0x40] = {
 '`','1','2','3','4','5','6','7','8','9','0','-','=','\\',0,'0',
 'q','w','e','r','t','y','u','i','o','p','[',']',0,'1','2','3',
 'a','s','d','f','g','h','j','k','l',';','\'','#',0,'4','5','6',
 '<','z','x','c','v','b','n','m',',','.','/',0,'.','7','8','9'
};
static const char km_hi[0x40] = {
 '~','!','@','#','$','%','^','&','*','(',')','_','+','|',0,'0',
 'Q','W','E','R','T','Y','U','I','O','P','{','}',0,'1','2','3',
 'A','S','D','F','G','H','J','K','L',':','"','~',0,'4','5','6',
 '>','Z','X','C','V','B','N','M','<','>','?',0,'.','7','8','9'
};

int key_note_offset(int code) { return (code >= 0 && code < 0x40) ? notemap[code] : -1; }

int key_digit(int code)
{
    if (code == K_0) return 0;
    if (code >= K_1 && code <= 0x09) return code;   /* 1..9 */
    return -1;
}

int key_hex(int code)
{
    int d = key_digit(code);
    if (d >= 0) return d;
    switch (code) {
    case K_A: return 10; case K_B: return 11; case K_C: return 12;
    case K_D: return 13; case K_E: return 14; case K_F: return 15;
    }
    return -1;
}

int key_ascii(int code, int shift)
{
    if (code == K_SPACE) return ' ';
    if (code < 0 || code >= 0x40) return 0;
    return shift ? km_hi[code] : km_lo[code];
}

void note_name(int note, char *out)
{
    static const char nn[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
    if (note == NOTE_NONE) { out[0] = '-'; out[1] = '-'; out[2] = '-'; }
    else if (note == NOTE_OFF) { out[0] = '='; out[1] = '='; out[2] = '='; }
    else {
        int n = (note - 1) % 12, o = (note - 1) / 12 + 1;
        out[0] = nn[n * 2]; out[1] = nn[n * 2 + 1]; out[2] = (char)('0' + o);
    }
    out[3] = 0;
}

int ui_parse_int(const char *s, int *out)
{
    int v = 0, neg = 0, any = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    if (!any) return 0;
    *out = neg ? -v : v;
    return 1;
}

Instr *cur_instr(App *a) { return &a->song->instr[a->ui.instr]; }

void ui_status(App *a, const char *msg)
{
    str_ncpy(a->ui.status, msg, sizeof(a->ui.status));
    a->ui.statusTimer = 50 * 4;
}

void ui_prompt(App *a, int kind, const char *label, const char *init)
{
    Ui *u = &a->ui;
    u->promptActive = 1;
    u->promptKind = kind;
    str_ncpy(u->promptLabel, label, sizeof(u->promptLabel));
    str_ncpy(u->promptBuf, init ? init : "", sizeof(u->promptBuf));
    u->promptLen = (int)strlen(u->promptBuf);
    u->promptFresh = 1;   /* first typed character replaces the suggested value */
}

void goto_screen(App *a, int scr)
{
    Ui *u = &a->ui;
    if (scr == u->screen) return;
    u->prevScreen = u->screen;
    u->screen = scr;
    if (scr == SCR_DISK) disk_refresh(a);
    if (scr == SCR_SAMPLE) {
        Instr *in = cur_instr(a);
        if (u->viewLen == 0 || u->viewStart + u->viewLen > in->length) { u->viewStart = 0; u->viewLen = in->length; }
    }
}

int ui_init(App *a)
{
    Ui *u = &a->ui;
    a->song = (Song *)mem_alloc(a, sizeof(Song), 0);
    if (!a->song) return 0;
    if (!song_init(a, a->song)) return 0;
    player_init(a);
    memset(u, 0, sizeof(Ui));
    u->octave = 3; u->instr = 1; u->step = 1; u->edit = 0;
    u->recRate = 22050; u->recSeconds = 4; u->recChan = 0;
    u->clip = (Cell *)mem_alloc(a, MAX_LINES * MAX_TRACKS * sizeof(Cell), 0);
    if (!u->clip) return 0;
    u->running = 1;
    ui_status(a, "Welcome to Paula's Revenge 1200. Press HELP for keys.");
    return 1;
}

void draw_header(App *a, const char *screenName)
{
    Song *s = a->song;
    Ui *u = &a->ui;
    char *t = a->tmp;
    int n;
    gfx_fill_row(a, 0, 0, COLS - 1, 1, 1);
    gfx_text(a, 1, 0, "PAULA'S REVENGE 1200", 0);
    gfx_text(a, 23, 0, screenName, 0);
    n = 0;
    memcpy(t + n, "BPM ", 4); n += 4; n += fmt_dec(t + n, s->bpm, 3);
    memcpy(t + n, "  SPD ", 6); n += 6; n += fmt_dec(t + n, s->speed, 2);
    memcpy(t + n, "  OCT ", 6); n += 6; n += fmt_dec(t + n, (u32)u->octave, 1);
    memcpy(t + n, "  INS ", 6); n += 6; n += fmt_hex(t + n, (u32)u->instr, 2);
    t[n] = 0;
    gfx_text(a, 41, 0, t, 0);
    if (a->pl.mode == PLAY_SONG) gfx_text(a, 74, 0, "SONG", 0);
    else if (a->pl.mode == PLAY_BLOCK) gfx_text(a, 74, 0, "LOOP", 0);
    else if (u->edit) { gfx_fill_row(a, 0, 74, 78, 2, 1); gfx_text(a, 74, 0, "EDIT", 0); }
    if (u->dirty) gfx_text(a, 79, 0, "*", 0);
}

void draw_fkeys(App *a, const char *hint)
{
    gfx_text(a, 0, 2, "F6 Block  F7 Arrange  F8 Sample  F9 Instr  F10 Disk  HELP Keys", 0);
    if (hint) gfx_text(a, 0, 23, hint, 0);
}

static void draw_status(App *a)
{
    Ui *u = &a->ui;
    gfx_fill_row(a, 24, 0, COLS - 1, 1, 1);
    if (u->promptActive) {
        int l = (int)strlen(u->promptLabel);
        gfx_fill_row(a, 24, 0, COLS - 1, 2, 1);
        gfx_text(a, 0, 24, u->promptLabel, 0);
        gfx_text(a, l + 1, 24, u->promptBuf, 0);
        if (u->promptFresh && u->promptLen)   /* underline: typing replaces this suggestion */
            gfx_hline(a, (l + 1) * 8, (l + 1 + u->promptLen) * 8 - 1, 24 * FONT_H + FONT_H - 1, 0);
        if ((a->frame >> 4) & 1) gfx_text(a, l + 1 + u->promptLen, 24, "_", 0);
    } else if (u->status[0]) {
        gfx_text(a, 1, 24, u->status, 0);
    }
}

static void prompt_done(App *a)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    int v = 0, ok = ui_parse_int(u->promptBuf, &v);
    const char *txt = u->promptBuf;
    int kind = u->promptKind;
    u->promptActive = 0;
    switch (kind) {
    case PR_QUIT:
        if (txt[0] == 'y' || txt[0] == 'Y') u->running = 0;
        break;
    case PR_NEW_SONG:
        if (txt[0] == 'y' || txt[0] == 'Y') {
            player_stop(a);
            song_free(a, s);
            song_init(a, s);
            u->block = 0; u->line = 0; u->track = 0; u->col = 0; u->trackOfs = 0;
            u->ordCur = 0; u->secSlotCur = 0; u->secListCur = 0;
            u->dirty = 0;
            ui_status(a, "New song.");
        }
        break;
    case PR_BLOCK_LINES:
        if (ok && v >= 1 && v <= MAX_LINES) { block_set_lines(s, u->block, v); u->dirty = 1; }
        block_ensure_valid(a);
        break;
    case PR_BLOCK_NAME: str_ncpy(s->blocks[u->block].name, txt, 16); u->dirty = 1; break;
    case PR_TRACKS:
        if (ok && v >= 1 && v <= MAX_TRACKS) { s->tracks = (u8)v; u->dirty = 1; }
        block_ensure_valid(a);
        break;
    case PR_SONG_NAME: str_ncpy(s->name, txt, 32); u->dirty = 1; break;
    case PR_BPM: if (ok && v >= 32 && v <= 255) { s->bpm = (u16)v; player_set_tempo(a, v); u->dirty = 1; } break;
    case PR_SPEED: if (ok && v >= 1 && v <= 32) { s->speed = (u8)v; a->pl.speed = v; u->dirty = 1; } break;
    case PR_SEC_NAME: {
        int sec = u->secListCur;
        if (sec >= 0 && sec < MAX_SECTIONS && s->sections[sec].used) { str_ncpy(s->sections[sec].name, txt, 16); u->dirty = 1; }
        break;
    }
    case PR_DEL_BLOCK:
        if (txt[0] == 'y' || txt[0] == 'Y') {
            int i, cnt = 0;
            for (i = 0; i < MAX_BLOCKS; i++) if (s->blocks[i].used) cnt++;
            if (cnt <= 1) { ui_status(a, "Can't delete the last block."); break; }
            player_stop(a);
            block_delete(a, s, u->block);
            u->dirty = 1;
            block_ensure_valid(a);
            ui_status(a, "Block deleted.");
        }
        break;
    case PR_DEL_SEC:
        if (txt[0] == 'y' || txt[0] == 'Y') {
            player_stop(a);
            section_delete(s, u->secListCur);
            if (u->ordCur >= s->orderLen) u->ordCur = s->orderLen ? s->orderLen - 1 : 0;
            u->dirty = 1;
            ui_status(a, "Section deleted.");
        }
        break;
    case PR_INS_NAME: str_ncpy(cur_instr(a)->name, txt, 24); u->dirty = 1; break;
    case PR_SAVE_SONG: {
        char *p = a->tmp;
        int l = (int)strlen(u->path);
        str_ncpy(p, u->path, 200);
        if (l && p[l - 1] != ':' && p[l - 1] != '/') { p[l++] = '/'; p[l] = 0; }
        str_ncpy(p + l, txt, 56);
        if (!str_icmp_suffix(p, ".prv")) { int m = (int)strlen(p); memcpy(p + m, ".prv", 5); }
        if (song_save(a, p)) { ui_status(a, "Song saved."); disk_refresh(a); }
        else ui_status(a, "Save failed!");
        break;
    }
    case PR_PATH:
        str_ncpy(u->path, txt, sizeof(u->path));
        disk_refresh(a);
        break;
    default:
        sample_prompt_done(a, kind, txt);
        break;
    }
}

static void prompt_key(App *a, const Event *e)
{
    Ui *u = &a->ui;
    int c;
    if (e->code == K_RETURN || e->code == K_KPENTER) { prompt_done(a); return; }
    if (e->code == K_ESC) { u->promptActive = 0; ui_status(a, "Cancelled."); return; }
    if (e->code == K_BACKSPACE) { u->promptFresh = 0; if (u->promptLen) u->promptBuf[--u->promptLen] = 0; return; }
    c = key_ascii(e->code, (e->qual & Q_SHIFT) != 0);
    if (c && u->promptFresh) { u->promptLen = 0; u->promptBuf[0] = 0; }
    if (c) u->promptFresh = 0;
    if (c && u->promptLen < (int)sizeof(u->promptBuf) - 1) {
        u->promptBuf[u->promptLen++] = (char)c;
        u->promptBuf[u->promptLen] = 0;
    }
}

static int global_key(App *a, const Event *e)
{
    Ui *u = &a->ui;
    int shift = (e->qual & Q_SHIFT) != 0;
    switch (e->code) {
    case K_F6: goto_screen(a, SCR_BLOCK); return 1;
    case K_F7: goto_screen(a, SCR_ARRANGE); return 1;
    case K_F8: goto_screen(a, SCR_SAMPLE); return 1;
    case K_F9: goto_screen(a, SCR_INSTR); return 1;
    case K_F10: disk_open(a, 0); return 1;
    case K_HELP: if (u->screen == SCR_HELP) goto_screen(a, u->prevScreen); else { u->help_page = 0; goto_screen(a, SCR_HELP); } return 1;
    case K_ESC:
        if (u->screen == SCR_HELP) { goto_screen(a, u->prevScreen); return 1; }
        ui_prompt(a, PR_QUIT, u->dirty ? "Song not saved! Quit? (y/n)" : "Quit? (y/n)", "");
        return 1;
    case K_KPMINUS: if (u->instr > 1) u->instr--; return 1;
    case K_KPPLUS: if (u->instr < MAX_INSTR - 1) u->instr++; return 1;
    }
    if (e->code >= K_F1 && e->code <= K_F5 && !shift) { u->octave = e->code - K_F1 + 1; return 1; }
    if (e->code == K_SPACE && a->pl.mode != PLAY_STOP) { player_stop(a); return 1; }
    if ((e->code == K_RETURN || e->code == K_KPENTER) && (u->screen == SCR_BLOCK || u->screen == SCR_ARRANGE)) {
        if (shift) player_play(a, PLAY_BLOCK, e->qual & Q_ALT ? 1 : 0);
        else player_play(a, PLAY_SONG, e->qual & Q_ALT ? 1 : 0);
        return 1;
    }
    /* song-wide settings: Alt+B bpm, Alt+S speed, Alt+N name, Alt+T tracks, Alt+I interpolation */
    if (e->qual & Q_ALT) {
        Song *s = a->song;
        char *t = a->tmp;
        switch (e->code) {
        case K_B: fmt_dec(t, s->bpm, 1); ui_prompt(a, PR_BPM, "Tempo BPM (32-255):", t); return 1;
        case K_S: fmt_dec(t, s->speed, 1); ui_prompt(a, PR_SPEED, "Ticks per line (1-32):", t); return 1;
        case K_N: ui_prompt(a, PR_SONG_NAME, "Song name:", s->name); return 1;
        case K_T: fmt_dec(t, s->tracks, 1); ui_prompt(a, PR_TRACKS, "Tracks (1-16):", t); return 1;
        case K_I:
            s->interp = !s->interp;
            ui_status(a, s->interp ? "Mixing: smooth (linear interpolation)" : "Mixing: Paula-style (no interpolation)");
            return 1;
        case K_V:
            if (shift) { if (s->masterVol > 0) s->masterVol -= 4; } else if (s->masterVol < 64) s->masterVol += 4;
            fmt_dec(t, s->masterVol, 1);
            ui_status(a, "Master volume changed.");
            return 1;
        }
    }
    return 0;
}

void ui_frame(App *a)
{
    Ui *u = &a->ui;
    Event e;
    while (plat_get_event(a, &e)) {
        if (e.type == EV_KEY) {
            if (!e.down) continue;
            if (u->promptActive) { prompt_key(a, &e); continue; }
            if (global_key(a, &e)) continue;
            switch (u->screen) {
            case SCR_BLOCK: block_key(a, &e); break;
            case SCR_ARRANGE: arrange_key(a, &e); break;
            case SCR_SAMPLE: sample_key(a, &e); break;
            case SCR_INSTR: instr_key(a, &e); break;
            case SCR_DISK: disk_key(a, &e); break;
            case SCR_HELP: help_key(a, &e); break;
            }
        } else if (u->screen == SCR_SAMPLE && !u->promptActive) {
            sample_mouse(a, &e);
        }
    }
    if (u->statusTimer > 0 && --u->statusTimer == 0) u->status[0] = 0;
    gfx_clear(a);
    switch (u->screen) {
    case SCR_BLOCK: block_draw(a); break;
    case SCR_ARRANGE: arrange_draw(a); break;
    case SCR_SAMPLE: sample_draw(a); break;
    case SCR_INSTR: instr_draw(a); break;
    case SCR_DISK: disk_draw(a); break;
    case SCR_HELP: help_draw(a); break;
    }
    draw_status(a);
    a->frame++;
}
