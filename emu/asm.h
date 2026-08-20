/* emu/asm.h — SymphonyPlus 迷你汇编器（.asm 文本 → 二进制）
 *
 * 支持：; 与 // 注释（字符串内不截断）、name: label（可与指令同行）、
 * U8/U16/U32/U64 裸数据（大端发射）、"..." 字符串（原样字节）、
 * 'A' 字符字面量、@0x… 对齐填充、指令行（同名多变体按操作数类型匹配）。
 * 立即数或 label 引用 > 0xFFFF 硬报错（不截断），未定义 label 报错。
 */
#ifndef SYMPLUS_ASM_H
#define SYMPLUS_ASM_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int line;           /* 1-based 出错行；0 = 无错误 */
    char msg[128];
} AsmError;

/* 汇编 text 到 out（cap 字节上限）。返回写入字节数；出错返回 -1 并填 err。 */
int asm_assemble(const char *text, uint8_t *out, size_t cap, AsmError *err);

#endif /* SYMPLUS_ASM_H */
