/* tests/emu_test.c — 指令模拟器语义测试 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "emu/emu.h"
#include "emu/asm.h"

static EmuResult run_src(const char *src) {
    uint8_t bin[4096];
    AsmError err;
    int n = asm_assemble(src, bin, sizeof bin, &err);
    assert(n > 0);
    EmuResult r = emu_run(bin, (size_t)n, 1 << 20, 10000000);
    assert(r.error == 0);
    return r;
}

int main(void) {
    /* 1. r1 = 2 + 3 */
    EmuResult r = run_src("mov r1, 2\nmov r2, 3\nadd r1, r1, r2\nhalt:\njmp halt");
    assert(r.regs[1] == 5);

    /* 2. 手写调用序列：main 调 sub（全栈传参），sub 返回 40*2=80 */
    r = run_src(
        "mov sp, 0x4000\n"
        "mov r1, 40\n"
        "sub sp, sp, 4\n"
        "store_32 [sp], r1\n"
        "call sub\n"
        "add sp, sp, 4\n"
        "jmp end\n"
        "sub:\n"
        "    add r9, sp, 4\n"      /* 无 offset 寻址：add 合成地址 */
        "    load_32 r2, [r9]\n"
        "    add r1, r2, r2\n"
        "    ret\n"
        "end:\n"
        "halt:\n"
        "    jmp halt\n");
    assert(r.regs[1] == 80);

    /* 3. 屏幕缓冲：向 0x2000 写 "Hi"（模拟器内存断言） */
    r = run_src(
        "mov r1, 0x2000\n"
        "mov r2, 'H'\n"            /* 字符字面量 → ASCII 72 */
        "store_8 [r1], r2\n"
        "add r1, r1, 1\n"          /* 无 offset 寻址：地址自增 */
        "mov r2, 'i'\n"
        "store_8 [r1], r2\n"
        "halt:\n"
        "    jmp halt\n");
    assert(memcmp(r.mem + 0x2000, "Hi", 2) == 0);

    /* 4. 停机约定：自跳转停止，exit_code = r1 */
    r = run_src("mov r1, 42\nhalt:\njmp halt");
    assert(r.exit_code == 42);

    /* 5. 大端数据读写：store_32 写后 load_32 读回原值 */
    r = run_src(
        "mov r1, 0x2000\n"
        "mov r2, 0x5678\n"            /* 0x12345678 超过 16 位立即数，分两段拼装 */
        "mov r3, 0x1234\n"
        "lsl r3, r3, 16\n"
        "or r2, r2, r3\n"
        "store_32 [r1], r2\n"
        "load_32 r3, [r1]\n"
        "halt:\n"
        "    jmp halt\n");
    assert(r.regs[3] == 0x12345678);
    assert(r.mem[0x2000] == 0x12 && r.mem[0x2001] == 0x34 &&
           r.mem[0x2002] == 0x56 && r.mem[0x2003] == 0x78);

    /* 6. 条件跳转（带符号与无符号） */
    r = run_src(
        "mov r1, 1\n"
        "nor r2, zr, zr\n"           /* r2 = 0xFFFFFFFF，有符号解释 = -1 */
        "cmp r1, r2\n"
        "jb yes_ub\n"                /* 1 < 0xFFFFFFFF 无符号成立 → 跳转 */
        "mov r1, 99\n"
        "jmp end\n"
        "yes_ub:\n"
        "cmp r2, r1\n"
        "jl yes_sl\n"                /* -1 < 1 有符号成立 → 跳转 */
        "mov r1, 98\n"
        "jmp end\n"
        "yes_sl:\n"
        "mov r1, 42\n"
        "end:\n"
        "halt:\n"
        "    jmp halt\n");
    assert(r.regs[1] == 42);

    printf("ALL PASS\n");
    return 0;
}
