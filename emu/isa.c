/* emu/isa.c — SymphonyPlus 指令编码表
 *
 * 全部 76 条：72 条单指令 + 4 条伪指令（push=2词, pop=2词, ret=3词, call=5词）。
 * 对照 SymphonyPlus.isa [instructions] 逐条转写。
 *
 * 表序注意：mov/neg/not 排在 or/sub/nor 之前——mov 的编码是 or 编码的严格
 * 子集（.isa 注释：mov ≡ or %a,%b,zr；其余同理），按表序先命中更特定的别名，
 * 语义一致。汇编器按"操作数类型匹配"选变体，不受此序影响。
 */

#include <string.h>
#include "emu/isa.h"

#define REG(p)   { OP_REG, { (p), -1 } }
#define IMM(p)   { OP_IMM, { (p), -1 } }
#define NONE     { OP_NONE, { -1, -1 } }

static const char *reg_names[16] = {
    "zr", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "sp", "flags",
};

const Insn isa_table[] = {
    /* ---------- IO（10） ---------- */
    { "nop", 1, {0x00000000}, {0xFFFFFFFF},
      {{NONE, NONE, NONE}}, 0 },
    { "in", 1, {0x01000000}, {0xFF00FFFF},
      {{REG(20), NONE, NONE}}, 1 },
    { "out", 1, {0x02000000}, {0xFFFF00FF},
      {{REG(8), NONE, NONE}}, 1 },
    { "out", 1, {0x12000000}, {0xFFFF0000},
      {{IMM(0), NONE, NONE}}, 1 },
    { "keyboard", 1, {0x03000000}, {0xFF00FFFF},
      {{REG(20), NONE, NONE}}, 1 },
    { "screen", 1, {0x04000000}, {0xFFF0F0FF},
      {{REG(16), REG(8), NONE}}, 2 },
    { "screen", 1, {0x14000000}, {0xFFF00000},
      {{REG(16), IMM(0), NONE}}, 2 },
    { "time_0", 1, {0x05000000}, {0xFF00FFFF},
      {{REG(20), NONE, NONE}}, 1 },
    { "time_1", 1, {0x06000000}, {0xFF00FFFF},
      {{REG(20), NONE, NONE}}, 1 },
    { "counter", 1, {0x07000000}, {0xFF00FFFF},
      {{REG(20), NONE, NONE}}, 1 },

    /* ---------- 别名（先于其 ALU 同族，行为一致） ---------- */
    { "mov", 1, {0x21000000}, {0xFFF0F0FF},       /* mov %a, %b = or %a, %b, zr */
      {{REG(20), REG(8), NONE}}, 2 },
    { "mov", 1, {0x31000000}, {0xFFF00000},       /* mov %a, %b = or %a, zr, %b */
      {{REG(20), IMM(0), NONE}}, 2 },
    { "neg", 1, {0x25000000}, {0xFFF0F0FF},       /* neg %a, %b = sub %a, zr, %b */
      {{REG(20), REG(8), NONE}}, 2 },
    { "neg", 1, {0x35000000}, {0xFFF00000},
      {{REG(20), IMM(0), NONE}}, 2 },
    { "not", 1, {0x23000000}, {0xFF00F0FF},       /* not %a, %b = nor %a, %b, %b */
      {{REG(20), {OP_REG, {16, 8}}, NONE}}, 2 },
    { "not", 1, {0x33000000}, {0xFFF00000},       /* not %a, %b = nor %a, zr, %b */
      {{REG(20), IMM(0), NONE}}, 2 },

    /* ---------- ALU 三操作数 reg（13） ---------- */
    { "nand", 1, {0x20000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "or",   1, {0x21000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "and",  1, {0x22000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "nor",  1, {0x23000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "add",  1, {0x24000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "sub",  1, {0x25000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "xor",  1, {0x26000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "lsl",  1, {0x27000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "lsr",  1, {0x28000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "asr",  1, {0x29000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "mul",  1, {0x2B000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "div",  1, {0x2C000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "mod",  1, {0x2D000000}, {0xFF00F0FF}, {{REG(20), REG(16), REG(8)}}, 3 },
    { "cmp",  1, {0x2A000000}, {0xFFF0F0FF},       /* cmp %a, %b */
      {{REG(16), REG(8), NONE}}, 2 },

    /* ---------- ALU 三操作数 imm（13） ---------- */
    { "nand", 1, {0x30000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "or",   1, {0x31000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "and",  1, {0x32000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "nor",  1, {0x33000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "add",  1, {0x34000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "sub",  1, {0x35000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "xor",  1, {0x36000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "lsl",  1, {0x37000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "lsr",  1, {0x38000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "asr",  1, {0x39000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "mul",  1, {0x3B000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "div",  1, {0x3C000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "mod",  1, {0x3D000000}, {0xFF000000}, {{REG(20), REG(16), IMM(0)}}, 3 },
    { "cmp",  1, {0x3A000000}, {0xFFF00000},       /* cmp %a, %b */
      {{REG(16), IMM(0), NONE}}, 2 },

    /* ---------- 跳转（12） ---------- */
    { "jmp", 1, {0x480F0000}, {0xFFFFF0FF},       /* jmp %a(reg) */
      {{REG(20), NONE, NONE}}, 1 },
    { "jmp", 1, {0x580F0000}, {0xFFFF0000},       /* jmp %a(imm) */
      {{IMM(0), NONE, NONE}}, 1 },
    { "je",   1, {0x510F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "jne",  1, {0x590F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "jb",   1, {0x520F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "jae",  1, {0x5A0F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "jbe",  1, {0x530F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "ja",   1, {0x5B0F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "jl",   1, {0x540F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "jge",  1, {0x5C0F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "jle",  1, {0x550F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },
    { "jg",   1, {0x5D0F0000}, {0xFFFF0000}, {{IMM(0), NONE, NONE}}, 1 },

    /* ---------- load/store（16） ---------- */
    { "load_8",  1, {0x60000000}, {0xFFF0F0FF}, {{REG(20), REG(8), NONE}}, 2 },
    { "load_16", 1, {0x61000000}, {0xFFF0F0FF}, {{REG(20), REG(8), NONE}}, 2 },
    { "load_32", 1, {0x62000000}, {0xFFF0F0FF}, {{REG(20), REG(8), NONE}}, 2 },
    { "pload",   1, {0x63000000}, {0xFFF0F0FF}, {{REG(20), REG(8), NONE}}, 2 },
    /* store 语法：store_* [%adr], %value —— adr 在前（bits 8-11/0-15），
     * value 在后（bits 16-19），按 .isa 位模式逐条核对 */
    { "store_8",  1, {0x64000000}, {0xFF0FF0FF}, {{REG(8), REG(16), NONE}}, 2 },
    { "store_16", 1, {0x65000000}, {0xFF0FF0FF}, {{REG(8), REG(16), NONE}}, 2 },
    { "store_32", 1, {0x66000000}, {0xFF0FF0FF}, {{REG(8), REG(16), NONE}}, 2 },
    { "pstore",   1, {0x67000000}, {0xFF0FF0FF}, {{REG(8), REG(16), NONE}}, 2 },
    { "load_8",  1, {0x70000000}, {0xFFF00000}, {{REG(20), IMM(0), NONE}}, 2 },
    { "load_16", 1, {0x71000000}, {0xFFF00000}, {{REG(20), IMM(0), NONE}}, 2 },
    { "load_32", 1, {0x72000000}, {0xFFF00000}, {{REG(20), IMM(0), NONE}}, 2 },
    { "pload",   1, {0x73000000}, {0xFFF00000}, {{REG(20), IMM(0), NONE}}, 2 },
    { "store_8",  1, {0x74000000}, {0xFF0F0000}, {{IMM(0), REG(16), NONE}}, 2 },
    { "store_16", 1, {0x75000000}, {0xFF0F0000}, {{IMM(0), REG(16), NONE}}, 2 },
    { "store_32", 1, {0x76000000}, {0xFF0F0000}, {{IMM(0), REG(16), NONE}}, 2 },
    { "pstore",   1, {0x77000000}, {0xFF0F0000}, {{IMM(0), REG(16), NONE}}, 2 },

    /* ---------- 伪指令（4） ---------- */
    /* push %a = sub sp,sp,4; store_32 [sp],%a */
    { "push", 2, {0x35EE0004, 0x66000E00}, {0xFFFFFFFF, 0xFFF0FFFF},
      {{NONE, NONE, NONE}, {REG(16), NONE, NONE}}, 1 },
    /* pop %a = load_32 %a,[sp]; add sp,sp,4 */
    { "pop", 2, {0x62000E00, 0x34EE0004}, {0xFFF0FFFF, 0xFFFFFFFF},
      {{REG(20), NONE, NONE}, {NONE, NONE, NONE}}, 1 },
    /* call %a = counter flags; add flags,flags,20; sub sp,sp,4;
       store_32 [sp],flags; jmp %a  （20 字节） */
    { "call", 5,
      {0x07F00000, 0x34FF0014, 0x35EE0004, 0x660F0E00, 0x580F0000},
      {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000},
      {{NONE, NONE, NONE}, {NONE, NONE, NONE}, {NONE, NONE, NONE},
       {NONE, NONE, NONE}, {IMM(0), NONE, NONE}}, 1 },
    /* ret = load_32 flags,[sp]; add sp,sp,4; jmp flags */
    { "ret", 3, {0x62F00E00, 0x34EE0004, 0x480F0F00},
      {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
      {{NONE, NONE, NONE}, {NONE, NONE, NONE}, {NONE, NONE, NONE}}, 0 },
};

const int isa_table_len = (int)(sizeof isa_table / sizeof isa_table[0]);

const Insn *isa_find(const char *name) {
    int i;
    for (i = 0; i < isa_table_len; i++)
        if (strcmp(isa_table[i].name, name) == 0)
            return &isa_table[i];
    return NULL;
}

int isa_reg_index(const char *name) {
    int i;
    for (i = 0; i < 16; i++)
        if (strcmp(reg_names[i], name) == 0)
            return i;
    return -1;
}

int isa_encode(const Insn *insn, const uint32_t *vals, uint8_t out[MAX_INSN_BYTES]) {
    int w, o, vi = 0;
    for (w = 0; w < insn->nwords; w++) {
        uint32_t word = insn->bits[w];
        for (o = 0; o < MAX_OPS; o++) {
            const OpField *f = &insn->ops[w][o];
            if (f->kind == OP_NONE)
                continue;
            uint32_t v = vals[vi++];
            word |= v << f->pos[0];
            if (f->pos[1] >= 0)
                word |= v << f->pos[1];
        }
        out[w * 4 + 0] = (uint8_t)(word >> 24);
        out[w * 4 + 1] = (uint8_t)(word >> 16);
        out[w * 4 + 2] = (uint8_t)(word >> 8);
        out[w * 4 + 3] = (uint8_t)(word);
    }
    return insn->nwords * 4;
}
