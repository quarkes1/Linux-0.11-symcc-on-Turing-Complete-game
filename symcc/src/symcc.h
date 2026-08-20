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
    TK_IDENT,    /* 标识符 */
    TK_KEYWORD,  /* 关键字（return 等） */
    TK_PUNCT,    /* 标点 + - * ( ) { } = ; , */
    TK_STR,      /* 字符串字面量（str/str_len 字段，转义已展开） */
};

typedef struct Token {
    struct Token *next;
    int kind;
    int64_t val;     /* TK_NUM 的值 */
    char *loc;       /* 源码位置（不含空白的首个字符） */
    int len;         /* 长度 */
    char *str;       /* TK_STR：展开后的字节（malloc，无 NUL 结尾） */
    int str_len;     /* TK_STR 字节数 */
    bool is_unsigned; /* TK_NUM：u/U 后缀 */
} Token;

Token *tokenize(const char *p);

/* 便捷谓词 */
bool tok_is(const Token *t, const char *s);   /* 标点/关键字文本比较 */
bool tok_is_kw(const Token *t, const char *kw);
int64_t tok_num(const Token *t);

/* ---------- 类型 ---------- */

typedef struct Type Type;
struct Type {
    enum { TY_INT, TY_CHAR, TY_PTR, TY_VOID } kind;
    Type *base;          /* TY_PTR：指向的类型 */
    bool is_unsigned;    /* 供 Task 8 有符号/无符号除法区分 */
};

extern Type *ty_int, *ty_char, *ty_void;   /* 单例（parse.c） */
Type *ty_ptr(Type *base);                  /* 指针类型（新建） */

/* ---------- 语法 ---------- */

enum {
    ND_NUM = 1,
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,      /* / 有符号调用 __divsi3，无符号直接 div */
    ND_MOD,      /* % 有符号调用 __modsi3，无符号直接 mod */
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
    ND_GVAR,     /* 全局变量：name = 全局名（label 即名字） */
    ND_CALL,     /* 函数调用：name = 函数名，rhs = 实参链表，val = 实参数 */
    ND_STR,      /* 字符串字面量：val = 字符串编号（数据段 label s%d） */
    ND_ADDR,     /* 一元 &：lhs = 变量（结果 = 地址） */
    ND_DEREF,    /* 一元 *：lhs = 指针表达式（结果 = 指向值） */
};

typedef struct Node {
    struct Node *next;
    int kind;
    struct Node *lhs;
    struct Node *rhs;
    struct Node *els;    /* if-else / for-inc */
    struct Node *body;   /* for 循环体 */
    Token *tok;    /* 生成诊断与代码注释用 */
    Type *ty;      /* 表达式/语句类型 */
    int64_t val;   /* ND_NUM / ND_CALL 实参数 / ND_STR 编号 */
    int offset;    /* ND_VAR：相对 sp 的负偏移 */
    char *name;    /* ND_GVAR / ND_CALL：标识符名（malloc） */
} Node;

/* 函数定义 */
typedef struct Func {
    struct Func *next;
    char *name;
    int len;
    Type *ret_ty;    /* 返回类型（TY_VOID = void） */
    int nargs;       /* 参数个数 */
    int frame_size;  /* 帧大小（局部变量 + 参数栈槽） */
    Node *body;      /* 语句链表 */
} Func;

/* 全局变量声明 */
typedef struct Global {
    struct Global *next;
    char *name;
    int len;
    Type *ty;
    int64_t init_val;   /* 初值（M1 常量） */
} Global;

/* 程序 = 全局变量 + 函数 + 字符串 */
typedef struct Program {
    Func *funcs;
    Global *globals;
    Token *strs;    /* 字符串字面量链表（顺序 = 编号） */
} Program;

/* 语法分析：多函数 + 全局变量。返回程序结构。 */
Program *parse(Token *tok);

/* ---------- 代码生成 ---------- */

/* 输出 SymphonyPlus 汇编文本到 out；imm 超出 16 位报错并返回 false */
bool codegen(Program *prog, FILE *out);

/* ---------- 编译入口（main.c 与测试运行器共用） ---------- */

/* 编译 C 源文本 src 到 out；失败（含超出 16 位立即数）返回 false。
 * 词法/语法错误直接退出进程（工具型编译器，M1 从简）。 */
bool symcc_compile_text(const char *src, FILE *out);

#endif /* SYMCC_H */
