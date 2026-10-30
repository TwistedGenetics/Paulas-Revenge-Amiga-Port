#ifndef TYPES_H
#define TYPES_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;

/* Hide where a pointer came from. LLVM 18 m68k drops the index register when a
   stack array is indexed by a variable; routing the base through an opaque
   register avoids that (tools/findframeidx.py enforces this at build time). */
#define OPAQUE(p) __asm__("" : "+r"(p))

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Freestanding libc subset (src/libc.c) */
void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
void *memmove(void *d, const void *s, unsigned long n);
int memcmp(const void *a, const void *b, unsigned long n);
unsigned long strlen(const char *s);
char *strcpy(char *d, const char *s);
int strcmp(const char *a, const char *b);
void str_ncpy(char *d, const char *s, int n); /* always terminates */
int str_icmp_suffix(const char *s, const char *suffix);
int fmt_hex(char *d, u32 v, int digits);
int fmt_dec(char *d, u32 v, int minDigits);
int fmt_sdec(char *d, s32 v);
int fmt_rdec(char *d, u32 v, int width); /* space padded, right aligned */

#endif
