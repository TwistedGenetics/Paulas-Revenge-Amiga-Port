/* Instrument list, disk browser, help pages. */
#include "ui_int.h"

#define INS_TOP 4
#define INS_ROWS 18

/* ---------------- instruments ---------------- */
static const char *const ins_fields[] = { "Vol", "Pan", "Fine", "Trans", "Rate", "SliceKey" };
#define NFIELDS 6

void instr_draw(App *a)
{
    Ui *u = &a->ui;
    Song *s = a->song;
    char *t = a->tmp;
    int r, first, n;
    draw_header(a, "INSTRUMENTS");
    gfx_text(a, 0, 1, "RETURN rename  L load  W save  DEL clear  LEFT/RIGHT field  +/- adjust", 0);
    draw_fkeys(a, "Play the keyboard to audition the selected instrument.");
    gfx_text(a, 0, 3, "## Name                    Length  Vol Pan Fine Trans  Rate  Loop  Slices", 0);
    {
        static const u8 fx[NFIELDS] = { 34, 38, 42, 47, 54, 66 };
        static const u8 fw[NFIELDS] = { 3, 3, 4, 5, 4, 6 };
        gfx_fill_row(a, 3, fx[u->insField], fx[u->insField] + fw[u->insField] - 1, 2, 1);
    }
    first = u->instr - INS_ROWS / 2;
    if (first < 1) first = 1;
    if (first > MAX_INSTR - INS_ROWS) first = MAX_INSTR - INS_ROWS;
    for (r = 0; r < INS_ROWS; r++) {
        int i = first + r;
        const Instr *in = &s->instr[i];
        if (i >= MAX_INSTR) break;
        memset(t, ' ', 80);
        fmt_hex(t, (u32)i, 2); t[2] = ' ';
        n = (int)strlen(in->name); if (n > 22) n = 22;
        memcpy(t + 3, in->name, (unsigned long)n);
        if (in->data) {
            fmt_rdec(t + 25, in->length, 7); t[32] = ' ';
            fmt_rdec(t + 34, in->volume, 2); t[36] = ' ';
            fmt_rdec(t + 38, in->pan, 3); t[41] = ' ';
            n = fmt_sdec(t + 42, in->finetune); t[42 + n] = ' ';
            n = fmt_sdec(t + 47, in->transpose); t[47 + n] = ' ';
            fmt_rdec(t + 52, in->baseRate, 6); t[58] = ' ';
            if (in->loopLen) memcpy(t + 60, "yes", 3); else memcpy(t + 60, " - ", 3);
            if (in->nslices) { n = fmt_rdec(t + 66, in->nslices, 2); t[66 + n] = ' '; note_name(in->sliceBase, t + 69); t[72] = ' '; }
        } else memcpy(t + 26, "(empty)", 7);
        t[79] = 0;
        gfx_text(a, 0, INS_TOP + r, t, 0);
        if (i == u->instr) gfx_fill_row(a, INS_TOP + r, 0, 79, 2, 1);
        else if (in->data) gfx_fill_row(a, INS_TOP + r, 0, 1, 1, 1);
    }
    (void)ins_fields;
}

static void adjust_field(App *a, int d)
{
    Instr *in = cur_instr(a);
    int v;
    audio_lock(a);
    switch (a->ui.insField) {
    case 0: v = in->volume + d; in->volume = (u8)(v < 0 ? 0 : v > 64 ? 64 : v); break;
    case 1: v = in->pan + d; in->pan = (u8)(v < 0 ? 0 : v > 255 ? 255 : v); break;
    case 2: v = in->finetune + d; in->finetune = (s8)(v < -8 ? -8 : v > 7 ? 7 : v); break;
    case 3: v = in->transpose + d; in->transpose = (s8)(v < -48 ? -48 : v > 48 ? 48 : v); break;
    case 4: v = (int)in->baseRate + d * 100; in->baseRate = (u32)(v < 1000 ? 1000 : v > 96000 ? 96000 : v); break;
    case 5: v = in->sliceBase + d; in->sliceBase = (u8)(v < 1 ? 1 : v > NOTE_MAX ? NOTE_MAX : v); break;
    }
    audio_unlock(a);
    a->ui.dirty = 1;
}

void instr_key(App *a, const Event *e)
{
    Ui *u = &a->ui;
    int code = e->code, shift = (e->qual & Q_SHIFT) != 0;
    switch (code) {
    case K_UP: if (u->instr > 1) u->instr--; return;
    case K_DOWN: if (u->instr < MAX_INSTR - 1) u->instr++; return;
    case K_LEFT: if (u->insField > 0) u->insField--; return;
    case K_RIGHT: if (u->insField < NFIELDS - 1) u->insField++; return;
    case K_EQUALS: adjust_field(a, shift ? 10 : 1); return;
    case K_MINUS: adjust_field(a, shift ? -10 : -1); return;
    case K_RETURN: ui_prompt(a, PR_INS_NAME, "Instrument name:", cur_instr(a)->name); return;
    case K_L: disk_open(a, 1); return;
    case K_W: ui_prompt(a, PR_SAVE_SAMPLE, "Save sample as (.8svx or .wav):", cur_instr(a)->name); return;
    case K_DEL: player_stop(a); instr_clear(a, cur_instr(a)); u->dirty = 1; ui_status(a, "Instrument cleared."); return;
    case K_SPACE: player_preview_stop(a); return;
    }
    {
        int off = key_note_offset(code);
        if (off >= 0) {
            int note = (u->octave - 1) * 12 + off + 1;
            if (note <= NOTE_MAX) player_preview(a, u->instr, note, 0, 0);
        }
    }
}

/* ---------------- disk ---------------- */
void disk_refresh(App *a)
{
    Ui *u = &a->ui;
    u->dirCount = dir_list(a, u->path, u->dir, MAX_DIR);
    if (u->dirCount < 0) { u->dirCount = 0; ui_status(a, "Can't read that directory."); }
    if (u->dirCur >= u->dirCount) u->dirCur = u->dirCount ? u->dirCount - 1 : 0;
    u->dirTop = 0;
}

void disk_open(App *a, int mode)
{
    a->ui.diskMode = mode;
    if (a->ui.screen == SCR_DISK) disk_refresh(a);
    else goto_screen(a, SCR_DISK);
}

static void path_parent(char *p)
{
    int l = (int)strlen(p);
    if (!l) return;
    if (p[l - 1] == ':') return;           /* already at the volume root */
    if (p[l - 1] == '/') p[--l] = 0;
    while (l > 0 && p[l - 1] != '/' && p[l - 1] != ':') l--;
    if (l > 0 && p[l - 1] == '/') l--;
    p[l] = 0;
}

static void path_join(char *out, const char *dir, const char *name)
{
    int l = (int)strlen(dir);
    str_ncpy(out, dir, 200);
    if (l && out[l - 1] != ':' && out[l - 1] != '/') out[l++] = '/';
    str_ncpy(out + l, name, 56);
}

void disk_draw(App *a)
{
    Ui *u = &a->ui;
    char *t = a->tmp;
    int r, rows = 17;
    draw_header(a, "DISK");
    gfx_text(a, 0, 1, u->diskMode == 1 ? "LOAD SAMPLE into the current instrument" : "LOAD SONG or sample (Return).  S save song.  N new song", 0);
    draw_fkeys(a, "UP/DN select  RETURN open  BACKSPACE parent  TAB type path (eg DH0:Songs)");
    t[0] = 0;
    memcpy(t, "Path: ", 6);
    str_ncpy(t + 6, u->path[0] ? u->path : "(current directory)", 70);
    gfx_text(a, 0, 3, t, 0);
    gfx_fill_row(a, 3, 0, 79, 1, 1);
    if (u->dirCur < u->dirTop) u->dirTop = u->dirCur;
    if (u->dirCur >= u->dirTop + rows) u->dirTop = u->dirCur - rows + 1;
    for (r = 0; r < rows && u->dirTop + r < u->dirCount; r++) {
        const DirEntry *d = &u->dir[u->dirTop + r];
        memset(t, ' ', 60);
        if (d->isDir) memcpy(t, "[DIR] ", 6);
        memcpy(t + 6, d->name, strlen(d->name));
        if (!d->isDir) fmt_rdec(t + 46, (u32)d->size, 9);
        t[56] = 0;
        gfx_text(a, 2, 5 + r, t, 0);
        if (u->dirTop + r == u->dirCur) gfx_fill_row(a, 5 + r, 1, 60, 2, 1);
    }
    if (!u->dirCount) gfx_text(a, 2, 5, "(empty)", 0);
}

static void disk_activate(App *a)
{
    Ui *u = &a->ui;
    const DirEntry *d;
    char *full = a->tmp;
    int r;
    if (!u->dirCount) return;
    d = &u->dir[u->dirCur];
    if (d->isDir) {
        path_join(full, u->path, d->name);
        str_ncpy(u->path, full, sizeof(u->path));
        u->dirCur = 0;
        disk_refresh(a);
        return;
    }
    path_join(full, u->path, d->name);
    if (u->diskMode == 1) {
        player_stop(a);
        instr_backup(a);
        r = sample_load(a, full, u->instr);
        ui_status(a, r > 0 ? "Sample loaded." : (r == -2 ? "Compressed 8SVX is not supported." : "Couldn't load that sample."));
        if (r > 0) { u->viewStart = 0; u->viewLen = cur_instr(a)->length; u->rangeA = u->rangeB = 0; goto_screen(a, SCR_SAMPLE); }
        return;
    }
    r = song_load(a, full);
    if (r == 1) {
        u->block = 0; u->line = 0; u->track = 0; u->col = 0; u->trackOfs = 0;
        u->ordCur = 0; u->secSlotCur = 0; u->secListCur = 0;
        block_ensure_valid(a);
        ui_status(a, "Song loaded.");
        goto_screen(a, SCR_ARRANGE);
    } else if (r == -1) {
        path_join(full, u->path, d->name);
        r = sample_load(a, full, u->instr);
        ui_status(a, r > 0 ? "Loaded as a sample into the current instrument." : "Not a song or a sample I can read.");
    } else if (r == -2) {
        ui_status(a, "Song file damaged: loaded what I could.");
        block_ensure_valid(a);
    } else ui_status(a, "Couldn't open that file.");
}

void disk_key(App *a, const Event *e)
{
    Ui *u = &a->ui;
    switch (e->code) {
    case K_UP: if (u->dirCur > 0) u->dirCur--; return;
    case K_DOWN: if (u->dirCur < u->dirCount - 1) u->dirCur++; return;
    case K_RETURN: case K_KPENTER: disk_activate(a); return;
    case K_BACKSPACE: path_parent(u->path); u->dirCur = 0; disk_refresh(a); return;
    case K_TAB: ui_prompt(a, PR_PATH, "Path:", u->path); return;
    case K_S: ui_prompt(a, PR_SAVE_SONG, "Save song as:", a->song->name); return;
    case K_N: ui_prompt(a, PR_NEW_SONG, "Clear everything and start a new song? (y/n)", ""); return;
    }
    if ((e->qual & Q_SHIFT) && e->code == K_UP) { u->dirCur = 0; return; }
}

/* ---------------- help ---------------- */
static const char *const help_text[] = {
"GLOBAL                                                   (any key: next page)",
" F6 Block  F7 Arrange  F8 Sample  F9 Instruments  F10 Disk  ESC quit",
" RETURN play song   Shift+RETURN loop block   Alt+RETURN from cursor",
" SPACE stop (or toggle EDIT in the block editor)",
" F1-F5 octave   Keypad -/+ instrument   Alt+B tempo   Alt+S speed",
" Alt+T tracks 1-16  Alt+N song name  Alt+I smooth mixing  Alt+V/Shift vol",
"BLOCK EDITOR",
" Z S X D C.. and Q 2 W 3 E.. notes (jam when not editing). 1 = note off",
" Arrows move  Shift+UP/DN 16 lines  TAB next track  Alt+0-9 edit step",
" DEL clear field (Shift: cell)  BACKSPACE delete line  Shift+BKSP insert",
" Ctrl+UP/DN change block  Ctrl+N new  Ctrl+D duplicate  Ctrl+DEL delete",
" Ctrl+L LINKED VARIATION: edited tracks become OWN, others follow",
" Ctrl+R relink track  Ctrl+B lines  Ctrl+E name  Ctrl+M mute (+Shift solo)",
" Ctrl+C/X/V copy/cut/paste track (add Shift for the whole block)",
"COMMANDS",
" 00 arpeggio  01/02 slide up/down  03 portamento  04 vibrato",
" 05/06 porta/vibrato + volume slide  08 pan (00 L, 80 C, FF R)",
" 09 speed  0B jump to timeline pos  0C volume 00-40  0D volume slide",
" 0F00 next block  0Fxx tempo  0FFE stop  0FFF note off",
" 19 sample offset (x256)  1Fxy delay x ticks, retrigger every y",
0,
"ARRANGE                                                  (any key: back)",
" TAB switch pane: timeline / section blocks / section library",
" Timeline: LEFT/RIGHT, I insert, DEL remove, Shift+arrows move, +/- swap",
" Section: A add current block  N new block  L slot to variation",
"          E edit block  +/- change block  Shift+UP/DN reorder",
" Library: N new  C copy  V copy as linked variations  I put on timeline",
" R rename section (any pane)",
"SAMPLE EDITOR",
" Drag with the mouse to select  A all  UP/DN zoom  Z zoom to range",
" LEFT/RIGHT scroll  SPACE play range  [ ] instrument  U undo/redo",
" N normalise  V reverse  I/O fade in/out  S silence  G gain",
" C crop  X cut  L loop=range  K loop off  F loop crossfade",
" P resample (semitones)  W save sample (.8svx or .wav)",
" H chop equal slices  T chop at transients  E remove slices",
" J copy slices to instruments.  Slice 1 plays on C-3, next on C#3 ...",
" R record from parallel port sampler  M input monitor  Q left/right",
"INSTRUMENTS  RETURN rename  L load  +/- edit field  keys audition",
"DISK  RETURN open  BACKSPACE parent  TAB type path  S save  N new song",
" Loads PR12 songs, IFF 8SVX, WAV, AIFF and raw 8-bit samples",
0
};

void help_draw(App *a)
{
    int i = 0, row;
    draw_header(a, "HELP");
    if (a->ui.help_page) { while (help_text[i]) i++; i++; }
    for (row = 1; help_text[i] && row < 24; i++, row++) {
        gfx_text(a, 0, row, help_text[i], 0);
        if (help_text[i][0] != ' ') gfx_fill_row(a, row, 0, COLS - 1, 1, 1);
    }
}

void help_key(App *a, const Event *e)
{
    (void)e;
    if (!a->ui.help_page) a->ui.help_page = 1;
    else { a->ui.help_page = 0; goto_screen(a, a->ui.prevScreen); }
}

