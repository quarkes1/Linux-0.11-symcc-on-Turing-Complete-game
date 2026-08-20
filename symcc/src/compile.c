/* symcc/src/compile.c — 编译入口（无 main，供 main.c 与测试运行器共用） */

#include <stdio.h>

#include "symcc.h"

/* preprocess_source_text（phase12 → tokenize → 预处理）→ parse → codegen；
 * 词法/预处理/语法错误直接退出进程。
 * 预处理器对无指令的纯 C 文本透明（M1 测试回归由 tests/preproc_test 与 run_tests 保证）。
 * 输出 .sym 对象文本（可重定位；Task 6 链接器消费）。 */
bool symcc_compile_text(const char *src, FILE *out) {
    Token *pp = preprocess_source_text(src, NULL, NULL, 0, NULL, 0);
    Program *prog = parse(pp);
    Obj *obj = obj_new();
    bool ok = codegen(prog, obj, false);
    if (ok)
        ok = obj_write(obj, out);
    obj_free(obj);
    return ok;
}
