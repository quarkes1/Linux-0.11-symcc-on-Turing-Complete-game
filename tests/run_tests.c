/* tests/run_tests.c — symcc 端到端测试运行器
 *
 * 链路（全进程内，无子进程）：symcc_compile_text（C→asm 文本）→
 * asm_assemble（asm→bin）→ emu_run（执行）→ 断言
 * 屏幕断言：mem[0x2000] 起为帧缓冲（M1 约定，见 emu/emu.h）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "emu/emu.h"
#include "emu/asm.h"
#include "symcc/src/symcc.h"

/* 编译（进程内 symcc）→ 汇编（进程内 asm_assemble）→ 模拟（emu_run）→ 断言 */
/* 大缓冲放 static（.bss）：Windows 默认栈仅 1MB，栈数组会溢出 */
static char text[1 << 20];
static uint8_t bin[1 << 20];

static void compile_and_run(const char *c_file, int expected_exit,
                            const char *screen, size_t screen_len,
                            const char *extra_file) {
    AsmError aerr;
    EmuResult res;
    int n;
    FILE *fp;
    size_t tlen;

    (void)extra_file;   /* Task 8 多文件编译时启用 */

    fp = fopen(c_file, "rb");
    assert(fp != NULL);
    tlen = fread(text, 1, sizeof text - 1, fp);
    fclose(fp);
    text[tlen] = 0;

    /* 编译 → 内存中的汇编文本 */
    FILE *afp = tmpfile();
    assert(afp != NULL);
    assert(symcc_compile_text(text, afp));
    rewind(afp);
    tlen = fread(text, 1, sizeof text - 1, afp);
    fclose(afp);
    text[tlen] = 0;

    n = asm_assemble(text, bin, sizeof bin, &aerr);
    assert(n > 0);
    res = emu_run(bin, (size_t)n, 1 << 20, 100000000);
    assert(res.error == 0);
    assert(res.exit_code == expected_exit);
    if (screen_len)
        assert(memcmp(res.mem + 0x2000, screen, screen_len) == 0);
    printf("PASS %s (exit=%d)\n", c_file, res.exit_code);
    free(res.mem);
    free(res.disk);
}

int main(void) {
    compile_and_run("tests/test_arith.c", 7, NULL, 0, "");
    printf("ALL TESTS PASSED\n");
    return 0;
}
