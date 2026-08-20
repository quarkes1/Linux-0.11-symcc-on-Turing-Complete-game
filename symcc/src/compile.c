/* symcc/src/compile.c — 编译入口（无 main，供 main.c 与测试运行器共用） */

#include <stdio.h>

#include "symcc.h"
#include "link.h"

/* preprocess_source_text（phase12 → tokenize → 预处理）→ parse → codegen；
 * 词法/预处理/语法错误直接退出进程。
 * 预处理器对无指令的纯 C 文本透明（M1 测试回归由 tests/preproc_test 与 run_tests 保证）。 */

/* 编译到内存可重定位对象（多文件链接用；d32 = 数据引用拆 32 位装载） */
bool symcc_compile_obj(const char *src, Obj *obj, bool d32) {
    Token *pp = preprocess_source_text(src, NULL, NULL, 0, NULL, 0);
    Program *prog = parse(pp);
    return codegen(prog, obj, d32);
}

/* 全链路：单文件 → 对象 → 链接（crt0 + halt）→ 绝对 asm 文本 */
bool symcc_compile_text(const char *src, FILE *out) {
    Obj *obj = obj_new();
    bool ok = symcc_compile_obj(src, obj, false);
    if (ok) {
        LinkError err;
        Obj *objs[1] = { obj };
        ok = symld_link(objs, 1, "runtime/crt0.asm", out, NULL, &err);
        if (!ok)
            fprintf(stderr, "symld: %s\n", err.msg);
    }
    obj_free(obj);
    return ok;
}
