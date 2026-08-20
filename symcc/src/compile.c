/* symcc/src/compile.c — 编译入口（无 main，供 main.c 与测试运行器共用） */

#include <stdio.h>

#include "symcc.h"

/* tokenize → parse → codegen；词法/语法错误直接退出进程 */
bool symcc_compile_text(const char *src, FILE *out) {
    Token *toks = tokenize(src);
    Program *prog = parse(toks);
    return codegen(prog, out);
}
