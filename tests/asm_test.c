/* tests/asm_test.c — 汇编器单元测试（大端期望字节） */

#include <stdio.h>
#include <string.h>
#include "emu/asm.h"

static int failures;

static void check(const char *name, const char *src, const char *want, int want_len) {
    uint8_t out[4096];
    AsmError err;
    int n = asm_assemble(src, out, sizeof out, &err);
    if (n < 0) {
        printf("FAIL %s: assemble error line %d: %s\n", name, err.line, err.msg);
        failures++;
        return;
    }
    if (n != want_len || (want && memcmp(out, want, (size_t)want_len) != 0)) {
        printf("FAIL %s: got %d bytes:", name, n);
        for (int i = 0; i < n && i < 16; i++) printf(" %02x", out[i]);
        printf("\n");
        failures++;
        return;
    }
    printf("PASS %s (%d bytes)\n", name, n);
}

static void check_err(const char *name, const char *src) {
    uint8_t out[4096];
    AsmError err;
    int n = asm_assemble(src, out, sizeof out, &err);
    if (n >= 0) {
        printf("FAIL %s: expected error, got %d bytes\n", name, n);
        failures++;
        return;
    }
    printf("PASS %s (error line %d: %s)\n", name, err.line, err.msg);
}

int main(void) {
    /* 1. ALU 三寄存器，大端 */
    check("add reg", "add r1, r2, r3", "\x24\x12\x03\x00", 4);
    /* 2. ALU 立即数 */
    check("add imm", "add r1, r2, 0x1234", "\x34\x12\x12\x34", 4);
    /* 3. label + jmp（自跳 = halt 模式）；label 值与绝对地址一致 */
    check("label+jmp", "mov r1, 1\nfirst:\njmp first",
          "\x31\x10\x00\x01\x58\x0f\x00\x04", 8);
    /* 4. 裸数据 + 字符串，大端 */
    check("data+str", "U32 0x12345678\n\"AB\"",
          "\x12\x34\x56\x78\x41\x42", 6);
    /* 5. 伪指令展开：push 8 + pop 8 + call 20 + ret 12 + jmp 4 = 52B */
    check("pseudos", "push r1\npop r2\ncall f\nret\nf:\njmp f", NULL, 52);
    /* 6. 硬错误：imm 越界 / 未定义 label / 未知助记符 / 操作数不足 */
    check_err("imm too large", "jmp 0x10000");
    check_err("undefined label", "jmp nowhere");
    check_err("bad mnemonic", "frobnicate r1, r2");
    check_err("bad operand count", "add r1, r2");
    /* 7. 注释与 'A' 字符字面量 */
    check("comment+char", "add r1, r2, 'A' ; 注释里没有影响\n// 整行注释",
          "\x34\x12\x00\x41", 4);
    /* 8. 内存寻址 [reg] / [imm]（load/store） */
    check("load imm", "load_32 r3, [0x2000]", "\x72\x30\x20\x00", 4);
    check("store reg", "store_8 [sp], r5", "\x64\x05\x0e\x00", 4);

    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("ALL PASS (10 checks)\n");
    return 0;
}
