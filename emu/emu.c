/* emu/emu.c — SymphonyPlus 指令模拟器
 *
 * 按 byte0 分发（每类语义唯一，见 isa.c 表序注释）：伪指令展开词本身就是
 * 普通指令（push-w0 = sub sp,sp,4；call-w4 = jmp imm 等），无需特殊处理。
 * mov/neg/not 是 or/sub/nor 的编码子集——直接按母指令语义解码（如 0x21 =
 * or/mov：regs[a] = regs[b@16] | regs[c@8]，mov 形式 b@16=zr 自然得 regs[a]=c）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"

#define DISK_SIZE (1 << 20)

/* 大端读写 */
static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* zr 恒为 0：写入丢弃 */
static void set_reg(uint32_t *regs, int i, uint32_t v) {
    if (i > 0) regs[i] = v;
}

EmuResult emu_run(const uint8_t *bin, size_t bin_len, size_t ram_size, uint64_t max_instr) {
    EmuResult r;
    uint32_t regs[16] = {0};
    uint32_t settings[8] = {0};
    uint32_t last_a = 0, last_b = 0;
    uint64_t pc = 0;
    uint64_t n = 0;
    int error = 0;

    memset(&r, 0, sizeof r);
    if (ram_size < 1) ram_size = 1;
    r.mem = (uint8_t *)calloc(ram_size, 1);
    r.mem_size = ram_size;
    r.disk = (uint8_t *)calloc(DISK_SIZE, 1);
    r.disk_size = DISK_SIZE;
    if (!r.mem || !r.disk) {
        error = 2;
        goto done;
    }
    if (bin_len > ram_size) {
        error = 2;                          /* 代码超出主存 */
        goto done;
    }
    memcpy(r.mem, bin, bin_len);

    while (n++ < max_instr) {
        uint32_t w;
        int b0, a, b, c;

        if (pc + 4 > ram_size) { error = 2; break; }
        w = rd32(r.mem + pc);
        b0 = (int)(w >> 24);
        a = (int)((w >> 20) & 0xF);
        b = (int)((w >> 16) & 0xF);
        c = (int)((w >> 8) & 0xF);

        switch (b0) {
        /* ---------- IO ---------- */
        case 0x00: break;                   /* nop */
        case 0x01: set_reg(regs, a, 0); break;                  /* in */
        case 0x02: printf("OUT %u\n", regs[(w >> 8) & 0xF]); break; /* out reg */
        case 0x03: set_reg(regs, a, 0); break;                  /* keyboard（空 FIFO） */
        case 0x04: settings[(w >> 16) & 0xF] = regs[(w >> 8) & 0xF]; break;
        case 0x05: set_reg(regs, a, 0); break;                  /* time_0 */
        case 0x06: set_reg(regs, a, 0); break;                  /* time_1 */
        case 0x07: set_reg(regs, a, (uint32_t)pc); break;       /* counter=自身地址 */
        case 0x10: printf("OUT %u\n", w & 0xFFFF); break;       /* out imm */
        case 0x14: settings[(w >> 16) & 0xF] = w & 0xFFFF; break;

        /* ---------- ALU reg（mov/neg/not 别名并入母指令） ---------- */
        case 0x20: set_reg(regs, a, ~(regs[b] & regs[c])); break;    /* nand */
        case 0x21: set_reg(regs, a, regs[b] | regs[c]); break;       /* or/mov */
        case 0x22: set_reg(regs, a, regs[b] & regs[c]); break;       /* and */
        case 0x23: set_reg(regs, a, ~(regs[b] | regs[c])); break;    /* nor/not */
        case 0x24: set_reg(regs, a, regs[b] + regs[c]); break;       /* add */
        case 0x25: set_reg(regs, a, regs[b] - regs[c]); break;       /* sub/neg */
        case 0x26: set_reg(regs, a, regs[b] ^ regs[c]); break;       /* xor */
        case 0x27: set_reg(regs, a, regs[b] << (regs[c] & 31)); break;   /* lsl */
        case 0x28: set_reg(regs, a, regs[b] >> (regs[c] & 31)); break;   /* lsr */
        case 0x29: set_reg(regs, a, (uint32_t)((int32_t)regs[b] >> (regs[c] & 31))); break; /* asr */
        case 0x2A: last_a = regs[b]; last_b = regs[c]; break;   /* cmp: a@19-16=b, b@11-8=c */
        case 0x2B: set_reg(regs, a, regs[b] * regs[c]); break;       /* mul（无符号） */
        case 0x2C: set_reg(regs, a, regs[c] ? regs[b] / regs[c] : 0); break; /* div */
        case 0x2D: set_reg(regs, a, regs[c] ? regs[b] % regs[c] : 0); break; /* mod */

        /* ---------- ALU imm ---------- */
        case 0x30: set_reg(regs, a, ~(regs[b] & (w & 0xFFFF))); break;
        case 0x31: set_reg(regs, a, regs[b] | (w & 0xFFFF)); break;  /* or/mov imm */
        case 0x32: set_reg(regs, a, regs[b] & (w & 0xFFFF)); break;
        case 0x33: set_reg(regs, a, ~(regs[b] | (w & 0xFFFF))); break; /* nor/not imm */
        case 0x34: set_reg(regs, a, regs[b] + (w & 0xFFFF)); break;  /* add imm */
        case 0x35: set_reg(regs, a, regs[b] - (w & 0xFFFF)); break;  /* sub/neg imm */
        case 0x36: set_reg(regs, a, regs[b] ^ (w & 0xFFFF)); break;
        case 0x37: set_reg(regs, a, regs[b] << ((w & 0xFFFF) & 31)); break;
        case 0x38: set_reg(regs, a, regs[b] >> ((w & 0xFFFF) & 31)); break;
        case 0x39: set_reg(regs, a, (uint32_t)((int32_t)regs[b] >> ((w & 0xFFFF) & 31))); break;
        case 0x3A: last_a = regs[b]; last_b = w & 0xFFFF; break;     /* cmp imm */
        case 0x3B: set_reg(regs, a, regs[b] * (w & 0xFFFF)); break;
        case 0x3C: set_reg(regs, a, (w & 0xFFFF) ? regs[b] / (w & 0xFFFF) : 0); break;
        case 0x3D: set_reg(regs, a, (w & 0xFFFF) ? regs[b] % (w & 0xFFFF) : 0); break;

        /* ---------- 跳转 ---------- */
        case 0x48: {                        /* jmp reg：reg@11-8（.isa 非标准位置） */
            uint32_t t = regs[(w >> 8) & 0xF];
            if (t == (uint32_t)pc) goto halt;
            pc = t;
            continue;
        }
        case 0x58: {                        /* jmp imm */
            uint32_t t = w & 0xFFFF;
            if (t == (uint32_t)pc) goto halt;
            pc = t;
            continue;
        }
        case 0x51: if (last_a == last_b) { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* je */
        case 0x59: if (last_a != last_b) { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* jne */
        case 0x52: if (last_a < last_b)  { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* jb 无符号 < */
        case 0x5A: if (last_a >= last_b) { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* jae 无符号 >= */
        case 0x53: if (last_a <= last_b) { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* jbe 无符号 <= */
        case 0x5B: if (last_a > last_b)  { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* ja 无符号 > */
        case 0x54: if ((int32_t)last_a < (int32_t)last_b)  { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* jl 有符号 < */
        case 0x5C: if ((int32_t)last_a >= (int32_t)last_b) { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* jge 有符号 >= */
        case 0x55: if ((int32_t)last_a <= (int32_t)last_b) { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* jle 有符号 <= */
        case 0x5D: if ((int32_t)last_a > (int32_t)last_b)  { if ((w & 0xFFFF) == pc) goto halt; pc = w & 0xFFFF; continue; } break; /* jg 有符号 > */

        /* ---------- load/store reg 寻址 ----------
         * load_* %dest, [%adr]：dest@20-23(a)，adr@8-11(c)
         * store_* [%adr], %value：adr@8-11(c)，value@16-19(b)
         * 越界检查用 uint64 加法防 32 位回绕（如 sp 未初始化时 sp-4 = 0xFFFFFFFC） */
        case 0x60: if ((uint64_t)regs[c] >= ram_size) { error = 2; goto done; } set_reg(regs, a, r.mem[regs[c]]); break;
        case 0x61: if ((uint64_t)regs[c] + 2 > ram_size) { error = 2; goto done; } set_reg(regs, a, rd16(r.mem + regs[c])); break;
        case 0x62: if ((uint64_t)regs[c] + 4 > ram_size) { error = 2; goto done; } set_reg(regs, a, rd32(r.mem + regs[c])); break;
        case 0x63: if ((uint64_t)regs[c] + 4 > r.disk_size) { error = 2; goto done; } set_reg(regs, a, rd32(r.disk + regs[c])); break; /* pload */
        case 0x64: if ((uint64_t)regs[c] >= ram_size) { error = 2; goto done; } r.mem[regs[c]] = (uint8_t)regs[b]; break;
        case 0x65: if ((uint64_t)regs[c] + 2 > ram_size) { error = 2; goto done; } wr16(r.mem + regs[c], (uint16_t)regs[b]); break;
        case 0x66: if ((uint64_t)regs[c] + 4 > ram_size) { error = 2; goto done; } wr32(r.mem + regs[c], regs[b]); break;
        case 0x67: if ((uint64_t)regs[c] + 4 > r.disk_size) { error = 2; goto done; } wr32(r.disk + regs[c], regs[b]); break; /* pstore */

        /* ---------- load/store imm 寻址 ----------
         * load_* %dest, [%adr]：dest@20-23(a)，adr imm@0-15
         * store_* [%adr], %value：adr imm@0-15，value@16-19(b) */
        case 0x70: if ((w & 0xFFFF) >= ram_size) { error = 2; goto done; } set_reg(regs, a, r.mem[w & 0xFFFF]); break;
        case 0x71: if ((w & 0xFFFF) + 2 > ram_size) { error = 2; goto done; } set_reg(regs, a, rd16(r.mem + (w & 0xFFFF))); break;
        case 0x72: if ((w & 0xFFFF) + 4 > ram_size) { error = 2; goto done; } set_reg(regs, a, rd32(r.mem + (w & 0xFFFF))); break;
        case 0x73: if ((w & 0xFFFF) + 4 > r.disk_size) { error = 2; goto done; } set_reg(regs, a, rd32(r.disk + (w & 0xFFFF))); break;
        case 0x74: if ((w & 0xFFFF) >= ram_size) { error = 2; goto done; } r.mem[w & 0xFFFF] = (uint8_t)regs[b]; break;
        case 0x75: if ((w & 0xFFFF) + 2 > ram_size) { error = 2; goto done; } wr16(r.mem + (w & 0xFFFF), (uint16_t)regs[b]); break;
        case 0x76: if ((w & 0xFFFF) + 4 > ram_size) { error = 2; goto done; } wr32(r.mem + (w & 0xFFFF), regs[b]); break;
        case 0x77: if ((w & 0xFFFF) + 4 > r.disk_size) { error = 2; goto done; } wr32(r.disk + (w & 0xFFFF), regs[b]); break;

        default:
            error = 3;                      /* 未知 opcode（不该发生） */
            goto done;
        }
        pc += 4;
    }
    if (n > max_instr) error = 1;           /* max_instr 超限 */

halt:
    r.exit_code = (int)regs[1];
done:
    r.error = error;
    memcpy(r.regs, regs, sizeof regs);
    memcpy(r.settings, settings, sizeof settings);
    return r;
}
