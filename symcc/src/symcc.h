/* symcc/src/symcc.h — SymphonyPlus C 编译器（M1：单函数、常量表达式）
 *
 * 单遍编译：tokenize → parse（递归下降）→ codegen（SymphonyPlus 汇编文本）。
 * 求值约定：表达式结果在 r1；二元运算右操作数经栈暂存。
 */

#ifndef SYMCC_H
#define SYMCC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------- 词法 ---------- */

enum {
    TK_EOF = 0,
    TK_NUM,      /* 数字字面量（val 字段） */
    TK_IDENT,    /* 标识符（M1 未用） */
    TK_KEYWORD,  /* 关键字（return） */
    TK_PUNCT,    /* 标点 + - * ( ) { } = ; , */
};

typedef struct Token {
    struct Token *next;
    int kind;
    int64_t val;   /* TK_NUM 的值 */
    char *loc;     /* 源码位置（不含空白的首个字符） */
    int len;       /* 长度 */
} Token;

Token *tokenize(const char *p);

/* 便捷谓词 */
bool tok_is(const Token *t, const char *s);   /* 标点/关键字文本比较 */
bool tok_is_kw(const Token *t, const char *kw);
int64_t tok_num(const Token *t);

/* ---------- 语法 ---------- */

enum {
    ND_NUM = 1,
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_NEG,
    ND_RETURN,   /* 语句：lhs = 返回值表达式 */
};

typedef struct Node {
    struct Node *next;
    int kind;
    struct Node *lhs;
    struct Node *rhs;
    Token *tok;    /* 生成诊断与代码注释用 */
    int64_t val;   /* ND_NUM */
} Node;

/* 函数体 = 语句列表。M1 只允许一个 main 函数。 */
Node *parse(Token *tok);

/* ---------- 代码生成 ---------- */

/* 输出 SymphonyPlus 汇编文本到 out；imm 超出 16 位报错并返回 false */
bool codegen(Node *prog, FILE *out);

/* ---------- 编译入口（main.c 与测试运行器共用） ---------- */

/* 编译 C 源文本 src 到 out；失败（含超出 16 位立即数）返回 false。
 * 词法/语法错误直接退出进程（工具型编译器，M1 从简）。 */
bool symcc_compile_text(const char *src, FILE *out);

#endif /* SYMCC_H */
