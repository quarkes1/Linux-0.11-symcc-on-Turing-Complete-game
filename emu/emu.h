/* emu/emu.h — SymphonyPlus 指令模拟器（PC 端开发验证用）
 *
 * 语义约定（对照 .isa 与游戏实测）：
 * - 全链路大端：指令取指与 8/16/32 位数据读写均大端
 *   （2026-08-20 实测：RAM 端序设置同时影响指令读取，统一 Big endian）
 * - 停机约定：跳转到自身地址（jmp 任意形式，目标 == 当前指令地址）→ 停止，
 *   exit_code = regs[1]
 * - flags 寄存器位布局未定义：cmp 记录 last_cmp=(a,b)，条件跳转直接依此判定
 * - div/mod 除数为 0 → 结果 0；移位量取 c 的低 5 位
 * - zr 恒为 0（写入丢弃）
 * - in/keyboard/time_0/time_1 → 目标寄存器置 0（M1 未使用）
 * - out → 打印数值到 stdout（调试用）
 * - screen a,b → settings[a] = b（不渲染）
 * - mem[0x2000] 起为帧缓冲（M1 约定）
 */
#ifndef SYMPLUS_EMU_H
#define SYMPLUS_EMU_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t regs[16];
    uint8_t *mem;           /* 主存（malloc，调用方 free） */
    size_t mem_size;
    uint8_t *disk;          /* 外存（1MB，M1 未使用；同上 free） */
    size_t disk_size;
    int exit_code;          /* 停机时 regs[1] */
    int error;              /* 0 正常；1 max_instr 超限；2 地址越界 */
    uint32_t settings[8];   /* 屏幕设置寄存器 */
} EmuResult;

/* bin 从地址 0 载入主存后开始执行。
 * ram_size = 主存字节数（2 的幂）；max_instr = 指令上限（防死循环）。 */
EmuResult emu_run(const uint8_t *bin, size_t bin_len, size_t ram_size, uint64_t max_instr);

#endif /* SYMPLUS_EMU_H */
