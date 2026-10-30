/* Minimal AmigaOS interface: library calls via inline asm (no NDK needed). */
#ifndef AMIGA_H
#define AMIGA_H
#include "../src/types.h"

/* jsr off(a6) is emitted as raw words: the LLVM m68k assembler can't parse it. */
#define SYSCALL(base, off, vd0, vd1, va0, va1) ({ \
    register u32 _d0 __asm__("d0") = (u32)(vd0); \
    register u32 _d1 __asm__("d1") = (u32)(vd1); \
    register u32 _a0 __asm__("a0") = (u32)(va0); \
    register u32 _a1 __asm__("a1") = (u32)(va1); \
    register void *_a6 __asm__("a6") = (void *)(base); \
    __asm__ volatile(".short 0x4EAE," #off \
        : "+r"(_d0), "+r"(_d1), "+r"(_a0), "+r"(_a1) : "r"(_a6) : "memory", "cc"); \
    _d0; })

#define SYSCALL_D23(base, off, vd1, vd2, vd3) ({ \
    register u32 _d0 __asm__("d0") = 0; \
    register u32 _d1 __asm__("d1") = (u32)(vd1); \
    register u32 _d2 __asm__("d2") = (u32)(vd2); \
    register u32 _d3 __asm__("d3") = (u32)(vd3); \
    register u32 _a0 __asm__("a0") = 0; \
    register u32 _a1 __asm__("a1") = 0; \
    register void *_a6 __asm__("a6") = (void *)(base); \
    __asm__ volatile(".short 0x4EAE," #off \
        : "+r"(_d0), "+r"(_d1), "+r"(_a0), "+r"(_a1) : "r"(_a6), "r"(_d2), "r"(_d3) : "memory", "cc"); \
    _d0; })

#define SYSBASE (*(void **)4)

/* exec.library */
#define AllocVec(sb, sz, fl)        ((void *)SYSCALL(sb, -684, sz, fl, 0, 0))
#define FreeVec(sb, p)              SYSCALL(sb, -690, 0, 0, 0, p)
#define OpenLibrary(sb, name, ver)  ((void *)SYSCALL(sb, -552, ver, 0, 0, name))
#define CloseLibrary(sb, lib)       SYSCALL(sb, -414, 0, 0, 0, lib)
#define FindTask(sb, name)          ((void *)SYSCALL(sb, -294, 0, 0, 0, name))
#define AllocSignal(sb, n)          ((s32)SYSCALL(sb, -330, n, 0, 0, 0))
#define FreeSignal(sb, n)           SYSCALL(sb, -336, n, 0, 0, 0)
#define Signal(sb, task, sigs)      SYSCALL(sb, -324, sigs, 0, 0, task)
#define Wait(sb, sigs)              SYSCALL(sb, -318, sigs, 0, 0, 0)
#define Forbid(sb)                  SYSCALL(sb, -132, 0, 0, 0, 0)
#define Permit(sb)                  SYSCALL(sb, -138, 0, 0, 0, 0)
#define Disable(sb)                 SYSCALL(sb, -120, 0, 0, 0, 0)
#define Enable(sb)                  SYSCALL(sb, -126, 0, 0, 0, 0)
#define WaitPort(sb, port)          SYSCALL(sb, -384, 0, 0, port, 0)
#define GetMsg(sb, port)            ((void *)SYSCALL(sb, -372, 0, 0, port, 0))
#define ReplyMsg(sb, msg)           SYSCALL(sb, -378, 0, 0, 0, msg)
#define AddIntServer(sb, n, is)     SYSCALL(sb, -168, n, 0, 0, is)
#define RemIntServer(sb, n, is)     SYSCALL(sb, -174, n, 0, 0, is)
#define CreateMsgPort(sb)           ((void *)SYSCALL(sb, -666, 0, 0, 0, 0))
#define DeleteMsgPort(sb, p)        SYSCALL(sb, -672, 0, 0, p, 0)
#define CreateIORequest(sb, p, sz)  ((void *)SYSCALL(sb, -654, sz, 0, p, 0))
#define DeleteIORequest(sb, io)     SYSCALL(sb, -660, 0, 0, io, 0)
#define OpenDevice(sb, nm, u, io, f) ((s32)SYSCALL(sb, -444, u, f, nm, io))
#define CloseDevice(sb, io)         SYSCALL(sb, -450, 0, 0, 0, io)
#define DoIO(sb, io)                SYSCALL(sb, -456, 0, 0, 0, io)
#define CacheClearU(sb)             SYSCALL(sb, -636, 0, 0, 0, 0)

#define MEMF_ANY   0
#define MEMF_CHIP  2
#define MEMF_CLEAR 0x10000

/* dos.library */
#define DOpen(db, name, mode)       SYSCALL_D23(db, -30, name, mode, 0)
#define DClose(db, fh)              SYSCALL_D23(db, -36, fh, 0, 0)
#define DRead(db, fh, buf, len)     ((s32)SYSCALL_D23(db, -42, fh, buf, len))
#define DWrite(db, fh, buf, len)    ((s32)SYSCALL_D23(db, -48, fh, buf, len))
#define DSeek(db, fh, pos, mode)    ((s32)SYSCALL_D23(db, -66, fh, pos, mode))
#define DLock(db, name, mode)       SYSCALL_D23(db, -84, name, mode, 0)
#define DUnLock(db, lock)           SYSCALL_D23(db, -90, lock, 0, 0)
#define DExamine(db, lock, fib)     SYSCALL_D23(db, -102, lock, fib, 0)
#define DExNext(db, lock, fib)      SYSCALL_D23(db, -108, lock, fib, 0)
#define AllocDosObject(db, t, tags) ((void *)SYSCALL_D23(db, -228, t, tags, 0))
#define FreeDosObject(db, t, p)     SYSCALL_D23(db, -234, t, p, 0)
#define MODE_OLDFILE 1005
#define MODE_NEWFILE 1006
#define ACCESS_READ  (-2)
#define DOS_FIB 2
#define OFFSET_BEGINNING (-1)
#define OFFSET_END 1

/* graphics / intuition */
#define LoadView(gb, v)             SYSCALL(gb, -222, 0, 0, 0, v)
#define WaitTOF(gb)                 SYSCALL(gb, -270, 0, 0, 0, 0)
#define RethinkDisplay(ib)          SYSCALL(ib, -390, 0, 0, 0, 0)
#define GFX_ACTIVIEW 34
#define GFX_COPINIT 38

/* structure offsets */
#define EXEC_ATTNFLAGS 296
#define PR_MSGPORT 92
#define PR_CLI 172
#define PR_WINDOWPTR 184
#define IO_COMMAND 28
#define IO_DATA 40
#define IND_ADDHANDLER 9
#define IND_REMHANDLER 10
#define FIB_DIRENTRYTYPE 4
#define FIB_FILENAME 8
#define FIB_SIZE 124

typedef struct {           /* struct Interrupt, 22 bytes */
    void *succ, *pred;
    u8 type; s8 pri;
    const char *name;
    void *data;
    void *code;
} __attribute__((packed)) Interrupt;

typedef struct { void *lower; u32 upper; void *pointer; } StackSwap;

/* custom chip registers */
#define CUSTOM ((volatile u16 *)0xDFF000)
#define R(off) (CUSTOM[(off) / 2])
#define DMACONR 0x002
#define VPOSR   0x004
#define INTREQR 0x01E
#define COP1LC  0x080
#define COPJMP1 0x088
#define DMACON  0x096
#define INTREQ  0x09C
#define AUD(n, r) (0x0A0 + (n) * 0x10 + (r))
#define AUD_LC  0
#define AUD_LEN 4
#define AUD_PER 6
#define AUD_VOL 8

#define CIAA_PRA  ((volatile u8 *)0xBFE001)
#define CIAA_PRB  ((volatile u8 *)0xBFE101)
#define CIAA_DDRB ((volatile u8 *)0xBFE301)
#define CIAB_PRA  ((volatile u8 *)0xBFD000)
#define CIAB_DDRA ((volatile u8 *)0xBFD200)

#endif
