#ifndef SIM_H
#define SIM_H
#include <stdint.h>
typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16; typedef int16_t s16;
typedef uint32_t u32; typedef int32_t s32; typedef uint64_t u64; typedef int64_t s64;

typedef struct {
    u32 d[8], a[8], pc, lastpc;
    int x, n, z, v, c;
    u64 steps;
} Cpu;
extern Cpu cpu;

void die(const char *fmt, ...);
u8 rd8(u32 a); u16 rd16(u32 a); u32 rd32(u32 a);
void wr8(u32 a, u32 v); void wr16(u32 a, u32 v); void wr32(u32 a, u32 v);
void os_call(u16 op);
void cpu_step(void);
void cpu_run_until(u32 until);
#endif
