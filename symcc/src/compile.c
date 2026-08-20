/* symcc/src/compile.c — 编译入口（无 main，供 main.c 与测试运行器共用） */

#include <stdio.h>

#include "symcc.h"
#include "link.h"

/* 预处理（phase12 → tokenize → 宏展开/指令，src_name 供相对 include）→
 * parse → codegen；词法/预处理/语法错误直接退出进程。
 * 预处理器对无指令的纯 C 文本透明（M1 测试回归由 tests/preproc_test 与 run_tests 保证）。 */

/* 编译到内存可重定位对象（多文件链接用；d32 = 数据引用拆 32 位装载）。
 * inc_dirs/defines 透传预处理（-I/-D；可为 NULL/0） */
bool symcc_compile_obj(const char *src, const char *src_name, Obj *obj,
                       bool d32, const char **inc_dirs, int n_inc,
                       const char **defines, int n_def) {
    Token *pp = preprocess_source_text(src, src_name, inc_dirs, n_inc,
                                       defines, n_def);
    Program *prog = parse(pp);
    return codegen(prog, obj, d32);
}
