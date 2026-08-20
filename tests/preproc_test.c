/* tests/preproc_test.c — 预处理器单测（对象宏/函数宏/条件/include/-D/续行）
 *
 * 注：test_defined_op 与 brief 相比有一处修正——brief 原版
 *   "#elif !defined(FOO) && defined(BAR)" 在 FOO/BAR 均未定义时为假，
 *   "int ok" 不可能被输出（任何正确语义的预处理器都一样）；改为
 *   "!defined(BAR)" 以保留测试意图（defined 括号形态/! /&&/elif 链）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symcc/src/symcc.h"

static int npass = 0, nfail = 0;
#define CHECK(cond) do { if (cond) { npass++; } else { nfail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char *pp(const char *src) {
    return preprocess_text(src, NULL, NULL, 0, NULL, 0);
}

static void test_object_macro(void) {
    char *out = pp("#define FOO 42\nint x = FOO;\n");
    CHECK(strstr(out, "x = 42"));
    CHECK(!strstr(out, "FOO"));
    free(out);
}
static void test_func_macro(void) {
    char *out = pp("#define ADD(a, b) ((a) + (b))\nint x = ADD(1, 2);\n");
    CHECK(strstr(out, "((1) + (2))"));
    free(out);
}
static void test_nested_macro(void) {
    char *out = pp("#define A 1\n#define B A\nint x = B;\n");
    CHECK(strstr(out, "x = 1"));
    free(out);
}
static void test_cond_ifdef(void) {
    char *out = pp("#define Y\n#ifdef Y\nint a;\n#else\nint b;\n#endif\n");
    CHECK(strstr(out, "int a"));
    CHECK(!strstr(out, "int b"));
    free(out);
}
static void test_if_expr(void) {
    char *out = pp("#define N 3\n#if N > 2\nint ok;\n#else\nint bad;\n#endif\n");
    CHECK(strstr(out, "int ok"));
    CHECK(!strstr(out, "int bad"));
    free(out);
}
static void test_if_undefined_is_zero(void) {
    char *out = pp("#if UNDEFINED_IDENT\nint bad;\n#else\nint ok;\n#endif\n");
    CHECK(strstr(out, "int ok"));
    free(out);
}
static void test_defined_op(void) {
    char *out = pp("#if defined(FOO)\nint bad;\n#elif !defined(FOO) && !defined(BAR)\nint ok;\n#endif\n");
    CHECK(strstr(out, "int ok"));
    free(out);
}
static void test_stringize(void) {
    char *out = pp("#define STR(x) #x\nchar *s = STR(hello);\n");
    CHECK(strstr(out, "\"hello\""));
    free(out);
}
static void test_paste(void) {
    char *out = pp("#define CAT(a, b) a##b\nint CAT(foo, bar);\n");
    CHECK(strstr(out, "foobar"));
    free(out);
}
static void test_line_continuation(void) {
    char *out = pp("#define LONG(x) \\\n    ((x) + 1)\nint y = LONG(5);\n");
    CHECK(strstr(out, "((5) + 1)"));
    free(out);
}
static void test_undef(void) {
    char *out = pp("#define X 1\n#undef X\n#if defined(X)\nint bad;\n#else\nint ok;\n#endif\n");
    CHECK(strstr(out, "int ok"));
    free(out);
}
static void test_cmdline_defines(void) {
    const char *defs[] = { "VERSION=7", "DEBUG" };
    char *out = preprocess_text("int v = VERSION;\n#ifdef DEBUG\nint dbg;\n#endif\n", NULL, NULL, 0, defs, 2);
    CHECK(strstr(out, "v = 7"));
    CHECK(strstr(out, "int dbg"));
    free(out);
}
static void test_comments_removed(void) {
    char *out = pp("int x; /* gone */ int y; // gone2\n");
    CHECK(strstr(out, "int x"));
    CHECK(strstr(out, "int y"));
    CHECK(!strstr(out, "gone"));
    free(out);
}

/* 已跳过的条件组内的嵌套 #if/#elif 不得求值（gcc 行为）：表达式即使
 * 本求值器无法解析（如孤立的 '('）也不能报错，组内内容不得输出。 */
static void test_skipped_if_no_eval(void) {
    char *out = pp("#if 0\n"
                   "#if (\n"
                   "int bad1;\n"
                   "#endif\n"
                   "#if 1\n"
                   "#elif (\n"
                   "#else\n"
                   "int bad2;\n"
                   "#endif\n"
                   "#endif\n"
                   "int ok;\n");
    CHECK(strstr(out, "int ok ;"));
    CHECK(!strstr(out, "bad1"));
    CHECK(!strstr(out, "bad2"));
    free(out);
}

/* #pragma 整行无操作；#error 在被跳过时忽略（取中分支的 #error 退出
 * 进程，无法在进程内测试——见 report 的驱动验证）。 */
static void test_pragma_error(void) {
    char *out = pp("#pragma once\n"
                   "#pragma pack(push, 1)\n"
                   "#if 0\n"
                   "#error this must be ignored\n"
                   "#endif\n"
                   "int ok;\n");
    CHECK(strstr(out, "int ok ;"));
    CHECK(!strstr(out, "pragma"));
    CHECK(!strstr(out, "ignored"));
    free(out);
}

int main(void) {
    test_object_macro();    test_func_macro();      test_nested_macro();
    test_cond_ifdef();      test_if_expr();         test_if_undefined_is_zero();
    test_defined_op();      test_stringize();       test_paste();
    test_line_continuation(); test_undef();         test_cmdline_defines();
    test_comments_removed(); test_skipped_if_no_eval(); test_pragma_error();
    printf("preproc_test: %d passed, %d failed\n", npass, nfail);
    return nfail ? 1 : 0;
}
