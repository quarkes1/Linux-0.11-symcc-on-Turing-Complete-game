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
    ND_VAR,      /* 表达式：读局部变量（offset 相对 sp，负值） */
    ND_ASSIGN,   /* lhs = 变量节点，rhs = 表达式 */
    ND_EQ,       /* == */
    ND_NE,       /* != */
    ND_LT,       /* <  有符号 */
    ND_LE,       /* <= 有符号 */
    ND_GT,       /* >  有符号 */
    ND_GE,       /* >= 有符号 */
    ND_LOGAND,   /* && 短路 */
    ND_LOGOR,    /* || 短路 */
    ND_NOT,      /* !  一元 */
    ND_RETURN,   /* 语句：lhs = 返回值表达式 */
    ND_IF,       /* lhs = 条件，rhs = then，els = else（可空） */
    ND_WHILE,    /* lhs = 条件，rhs = 循环体 */
    ND_FOR,      /* lhs = init（可空），rhs = 条件（可空），els = inc（可空），body = 循环体 */
};

typedef struct Node {
    struct Node *next;
    int kind;
    struct Node *lhs;
    struct Node *rhs;
    struct Node *els;    /* if-else / for-inc */
    struct Node *body;   /* for 循环体 */
    Token *tok;    /* 生成诊断与代码注释用 */
    int64_t val;   /* ND_NUM */
    int offset;    /* ND_VAR：相对 sp 的负偏移 */
} Node;

/* 函数体 = 语句列表。M1 只允许一个 main 函数。 */
Node *parse(Token *tok);

/* 当前函数的栈帧大小（局部变量总字节数；M1 单函数，解析完成后可查） */
int symcc_frame_size(void);

/* ---------- 代码生成 ---------- */

/* 输出 SymphonyPlus 汇编文本到 out；imm 超出 16 位报错并返回 false */
bool codegen(Node *prog, FILE *out);

/* ---------- 编译入口（main.c 与测试运行器共用） ---------- */

/* 编译 C 源文本 src 到 out；失败（含超出 16 位立即数）返回 false。
 * 词法/语法错误直接退出进程（工具型编译器，M1 从简）。 */
bool symcc_compile_text(const char *src, FILE *out);

#endif /* SYMCC_H */
