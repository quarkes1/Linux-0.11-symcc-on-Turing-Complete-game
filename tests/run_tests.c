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

    /* 多文件：先读额外文件（runtime 等，逗号分隔多个），再读被测文件。
     * 顺序要求：被调函数先定义（M1 单遍；symcc.exe 多文件同样按
     * 命令行顺序拼接）。 */
    tlen = 0;
    if (extra_file[0]) {
        char list[256];
        strncpy(list, extra_file, sizeof list - 1);
        list[sizeof list - 1] = 0;
        char *rest = list;
        while (rest && *rest) {
            char *comma = strchr(rest, ',');
            if (comma)
                *comma = 0;
            fp = fopen(rest, "rb");
            assert(fp != NULL);
            tlen += fread(text + tlen, 1, sizeof text - 1 - tlen, fp);
            fclose(fp);
            text[tlen++] = '\n';          /* 文件间换行分隔 */
            rest = comma ? comma + 1 : NULL;
        }
        if (tlen >= sizeof text - 2) {
            fprintf(stderr, "text buffer overflow (%s)\n", extra_file);
            exit(1);
        }
    }
    fp = fopen(c_file, "rb");
    assert(fp != NULL);
    tlen += fread(text + tlen, 1, sizeof text - 1 - tlen, fp);
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
    res = emu_run(bin, (size_t)n, 8 << 20, 100000000);
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
    compile_and_run("tests/test_locals.c", 13, NULL, 0, "");
    compile_and_run("tests/test_control.c", 100, NULL, 0, "");
    compile_and_run("tests/test_fun.c", 162, NULL, 0, "");
    compile_and_run("tests/test_ptr.c", 15, NULL, 0, "");
    compile_and_run("tests/test_char.c", 66, NULL, 0, "");
    compile_and_run("tests/test_div.c", 1, NULL, 0, "runtime/divsi3.c");
    compile_and_run("tests/test_udiv.c", 1, NULL, 0, "runtime/divsi3.c");
    /* Hello 验收：帧缓冲 mem[0x2000:0x2005] == "Hello"，exit 42 */
    compile_and_run("tests/test_hello.c", 42, "Hello", 5,
                    "runtime/tty.c,runtime/divsi3.c");
    printf("ALL TESTS PASSED\n");
    return 0;
}
