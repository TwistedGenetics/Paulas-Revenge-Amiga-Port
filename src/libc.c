/* Minimal freestanding C library. No global state anywhere. */
#include "types.h"

#ifndef HOST

void *memset(void *d, int c, unsigned long n)
{
    u8 *p = (u8 *)d;
    while (n && ((unsigned long)p & 3)) { *p++ = (u8)c; n--; }
    if (n >= 4) {
        u32 v = (u8)c; v |= v << 8; v |= v << 16;
        u32 *q = (u32 *)p;
        while (n >= 4) { *q++ = v; n -= 4; }
        p = (u8 *)q;
    }
    while (n--) *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
    u8 *dp = (u8 *)d; const u8 *sp = (const u8 *)s;
    if ((((unsigned long)dp | (unsigned long)sp) & 3) == 0) {
        u32 *dq = (u32 *)dp; const u32 *sq = (const u32 *)sp;
        while (n >= 4) { *dq++ = *sq++; n -= 4; }
        dp = (u8 *)dq; sp = (const u8 *)sq;
    }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, unsigned long n)
{
    u8 *dp = (u8 *)d; const u8 *sp = (const u8 *)s;
    if (dp == sp || n == 0) return d;
    if (dp < sp || dp >= sp + n) return memcpy(d, s, n);
    dp += n; sp += n;
    while (n--) *--dp = *--sp;
    return d;
}

int memcmp(const void *a, const void *b, unsigned long n)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}

unsigned long strlen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0)
        continue;
    return r;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

/* compiler helpers: DIVSL.L d2,d1:d0 / DIVUL.L d2,d1:d0 emitted as raw words */
int __modsi3(int a, int b)
{
    register int d0 __asm__("d0") = a;
    register int d1 __asm__("d1") = 0;
    register int d2 __asm__("d2") = b;
    __asm__(".short 0x4C42,0x0801" : "+r"(d0), "+r"(d1) : "r"(d2) : "cc");
    return d1;
}

unsigned int __umodsi3(unsigned int a, unsigned int b)
{
    register unsigned int d0 __asm__("d0") = a;
    register unsigned int d1 __asm__("d1") = 0;
    register unsigned int d2 __asm__("d2") = b;
    __asm__(".short 0x4C42,0x0001" : "+r"(d0), "+r"(d1) : "r"(d2) : "cc");
    return d1;
}

int __divsi3(int a, int b)
{
    register int d0 __asm__("d0") = a;
    register int d1 __asm__("d1") = 0;
    register int d2 __asm__("d2") = b;
    __asm__(".short 0x4C42,0x0801" : "+r"(d0), "+r"(d1) : "r"(d2) : "cc");
    return d0;
}

unsigned int __udivsi3(unsigned int a, unsigned int b)
{
    register unsigned int d0 __asm__("d0") = a;
    register unsigned int d1 __asm__("d1") = 0;
    register unsigned int d2 __asm__("d2") = b;
    __asm__(".short 0x4C42,0x0001" : "+r"(d0), "+r"(d1) : "r"(d2) : "cc");
    return d0;
}

#endif /* HOST */

void str_ncpy(char *d, const char *s, int n)
{
    int i = 0;
    if (n <= 0) return;
    while (i < n - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int str_icmp_suffix(const char *s, const char *suffix)
{
    int ls = (int)strlen(s), lf = (int)strlen(suffix), i;
    if (lf > ls) return 0;
    for (i = 0; i < lf; i++) if (lc(s[ls - lf + i]) != lc(suffix[i])) return 0;
    return 1;
}

int fmt_hex(char *d, u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        int n = v & 15; v >>= 4;
        d[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
    }
    d[digits] = 0;
    return digits;
}

int fmt_dec(char *d, u32 v, int minDigits)
{
    char tb[12], *t = tb; int n = 0, i;
    OPAQUE(t);
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n < minDigits) t[n++] = '0';
    for (i = 0; i < n; i++) d[i] = t[n - 1 - i];
    d[n] = 0;
    return n;
}

int fmt_rdec(char *d, u32 v, int width)
{
    char tb[12], *t = tb;
    int n, i, pad;
    OPAQUE(t);
    n = fmt_dec(t, v, 1); pad = width - n;
    if (pad < 0) pad = 0;
    for (i = 0; i < pad; i++) d[i] = ' ';
    memcpy(d + pad, t, (unsigned long)n + 1);
    return pad + n;
}

int fmt_sdec(char *d, s32 v)
{
    if (v < 0) { d[0] = '-'; return 1 + fmt_dec(d + 1, (u32)(-v), 1); }
    return fmt_dec(d, (u32)v, 1);
}
