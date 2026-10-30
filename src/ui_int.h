#ifndef UI_INT_H
#define UI_INT_H
#include "app.h"

/* Amiga raw key codes */
#define K_BACKTICK 0x00
#define K_1 0x01
#define K_0 0x0A
#define K_MINUS 0x0B
#define K_EQUALS 0x0C
#define K_LBRACK 0x1A
#define K_RBRACK 0x1B
#define K_A 0x20
#define K_SPACE 0x40
#define K_BACKSPACE 0x41
#define K_TAB 0x42
#define K_KPENTER 0x43
#define K_RETURN 0x44
#define K_ESC 0x45
#define K_DEL 0x46
#define K_KPMINUS 0x4A
#define K_UP 0x4C
#define K_DOWN 0x4D
#define K_RIGHT 0x4E
#define K_LEFT 0x4F
#define K_F1 0x50
#define K_F5 0x54
#define K_F6 0x55
#define K_F7 0x56
#define K_F8 0x57
#define K_F9 0x58
#define K_F10 0x59
#define K_KPPLUS 0x5E
#define K_HELP 0x5F

/* letters */
#define K_Q 0x10
#define K_W 0x11
#define K_E 0x12
#define K_R 0x13
#define K_T 0x14
#define K_Y 0x15
#define K_U 0x16
#define K_I 0x17
#define K_O 0x18
#define K_P 0x19
#define K_S 0x21
#define K_D 0x22
#define K_F 0x23
#define K_G 0x24
#define K_H 0x25
#define K_J 0x26
#define K_K 0x27
#define K_L 0x28
#define K_Z 0x31
#define K_X 0x32
#define K_C 0x33
#define K_V 0x34
#define K_B 0x35
#define K_N 0x36
#define K_M 0x37

/* prompt kinds */
#define PR_NONE 0
#define PR_QUIT 1
#define PR_BLOCK_LINES 2
#define PR_BLOCK_NAME 3
#define PR_TRACKS 4
#define PR_SONG_NAME 5
#define PR_BPM 6
#define PR_SPEED 7
#define PR_SEC_NAME 8
#define PR_INS_NAME 9
#define PR_GAIN 10
#define PR_RESAMPLE 11
#define PR_CHOP_N 12
#define PR_CHOP_T 13
#define PR_XFADE 14
#define PR_SAVE_SONG 15
#define PR_SAVE_SAMPLE 16
#define PR_PATH 17
#define PR_REC_RATE 18
#define PR_REC_SECS 19
#define PR_DEL_BLOCK 20
#define PR_DEL_SEC 21
#define PR_NEW_SONG 22

/* ui.c */
void ui_prompt(App *a, int kind, const char *label, const char *init);
int  key_note_offset(int code);       /* -1 if not a note key */
int  key_hex(int code);               /* -1 if not hex */
int  key_ascii(int code, int shift);  /* 0 if none */
int  key_digit(int code);
void note_name(int note, char *out);  /* 3 chars + nul */
void draw_header(App *a, const char *screenName);
void draw_fkeys(App *a, const char *hint);
void goto_screen(App *a, int scr);
int  ui_parse_int(const char *s, int *out);
Instr *cur_instr(App *a);
void instr_backup(App *a);

/* ui_block.c */
void block_draw(App *a);
void block_key(App *a, const Event *e);
void block_ensure_valid(App *a);

/* ui_arrange.c */
void arrange_draw(App *a);
void arrange_key(App *a, const Event *e);

/* ui_sample.c */
void sample_draw(App *a);
void sample_key(App *a, const Event *e);
void sample_mouse(App *a, const Event *e);
void sample_prompt_done(App *a, int kind, const char *txt);
void sample_undo(App *a);

/* ui_misc.c */
void instr_draw(App *a);
void instr_key(App *a, const Event *e);
void disk_draw(App *a);
void disk_key(App *a, const Event *e);
void disk_refresh(App *a);
void disk_open(App *a, int mode);
void help_draw(App *a);
void help_key(App *a, const Event *e);

#endif
