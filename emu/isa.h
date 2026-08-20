/* emu/isa.h — SymphonyPlus 指令编码表（汇编器与模拟器共享）
 *
 * 编码模型：指令字 32 位，byte0 = bits 31..24（与 .isa 位模式从左到右的
 * 4 组字节一致）；写入字节流时 byte0 在前（大端）。操作数字段用 bit 位置
 * （字段 LSB 的位号）描述：OP_REG = 4 位，OP_IMM = 16 位。
 * 伪指令（push/pop/call/ret）展开为多词，每词都有独立 bits/mask。
 */
#ifndef SYMPLUS_ISA_H
#define SYMPLUS_ISA_H

#include <stdint.h>
#include <stddef.h>

enum { OP_NONE, OP_REG, OP_IMM };
enum { MAX_OPS = 3, MAX_WORDS = 5 };      /* call = 5 词 */
#define MAX_INSN_BYTES (MAX_WORDS * 4)

typedef struct {
    int kind;           /* OP_NONE / OP_REG / OP_IMM */
    int pos[2];         /* 位域位置（LSB）；pos[1] = -1，或第二处位域（not 类） */
} OpField;

typedef struct {
    const char *name;
    int nwords;                 /* 1 = 普通指令；2/3/5 = 伪指令 */
    uint32_t bits[MAX_WORDS];   /* 每词固定位 */
    uint32_t mask[MAX_WORDS];   /* 每词固定位掩码（字段位为 0） */
    OpField ops[MAX_WORDS][3];  /* 每词操作数位域（词序优先，跨词依次编号） */
    int opcount;                /* 总操作数 */
} Insn;

extern const Insn isa_table[];
extern const int isa_table_len;

/* 按名字查找（返回第一个匹配；同名多变体由调用方遍历 isa_table） */
const Insn *isa_find(const char *name);

/* 编码：vals[i] = 第 i 个操作数的值（寄存器为 0..15）。
 * 返回总字节数（nwords*4）；out 需至少 MAX_INSN_BYTES。 */
int isa_encode(const Insn *insn, const uint32_t *vals, uint8_t out[MAX_INSN_BYTES]);

/* 寄存器名 zr..flags → 0..15；非寄存器返回 -1 */
int isa_reg_index(const char *name);

#endif /* SYMPLUS_ISA_H */
