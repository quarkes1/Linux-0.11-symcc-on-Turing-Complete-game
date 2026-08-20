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

#include "obj.h"

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
    bool at_bol;     /* 位于其源码行的行首（预处理器用） */
} Token;

Token *tokenize(const char *p);

/* 便捷谓词 */
bool tok_is(const Token *t, const char *s);   /* 标点/关键字文本比较 */
bool tok_is_kw(const Token *t, const char *kw);
int64_t tok_num(const Token *t);
bool tok_at_bol(const Token *t);              /* 该 token 是否位于其源码行的行首 */

/* ---------- 预处理器（M2 Task 1） ---------- */

/* 宏展开/条件编译/#include 展开后的 token 流（供 parse 消费；失败 exit(1)）。
 * src_name = 当前源文件路径（解析 #include "..." 相对目录用；NULL = 未知/stdin）。
 * inc_dirs = -I 目录数组；defines = -D 数组（"NAME" 或 "NAME=VALUE"）。
 * 返回链以 TK_EOF 结尾（malloc，调用方不释放——工具编译器）。 */
Token *preprocess_tokens(Token *tok, const char *src_name,
                         const char **inc_dirs, int n_inc,
                         const char **defines, int n_def);

/* 一站式：phase12（续行合并+注释剥离）→ tokenize → preprocess_tokens。
 * 返回链 loc 指向内部缓冲（按进程生命周期存活；调用方不 free）。 */
Token *preprocess_source_text(const char *src, const char *src_name,
                              const char **inc_dirs, int n_inc,
                              const char **defines, int n_def);

/* -E 用：tokenize → preprocess_tokens → 文本重建（malloc，调用方 free） */
char *preprocess_text(const char *src, const char *src_name,
                      const char **inc_dirs, int n_inc,
                      const char **defines, int n_def);

/* ---------- 类型 ---------- */

typedef struct Type Type;
typedef struct Member Member;
struct Member {
    struct Member *next;
    char *name;
    int len;
    Type *ty;
    int offset;          /* STRUCT: 字节偏移；UNION: 0；ENUM: 常量值 */
    int bit_offset;      /* 位域：存储单元（4 字节）内位偏移，大端最高位起；-1 = 非位域 */
    int bit_width;       /* 位域宽度；-1 = 非位域 */
};

typedef struct Type Type;
struct Type {
    enum { TY_INT, TY_CHAR, TY_PTR, TY_VOID, TY_ARRAY, TY_STRUCT, TY_UNION, TY_FUNC } kind;
    Type *base;          /* PTR: 指向的类型；ARRAY: 元素类型；FUNC: 返回类型 */
    bool is_unsigned;    /* 供有符号/无符号除法、比较、右移区分 */
    int64_t array_len;   /* TY_ARRAY：元素数；-1 = 不完整（extern/形参/推断） */
    Member *members;     /* TY_STRUCT/TY_UNION */
    int size;            /* 布局字节数（STRUCT/UNION 4 对齐；ARRAY 元素总字节） */
    char *tag;           /* struct/union/enum 标签（malloc 或 NULL） */
    /* TY_FUNC 参数信息（func_type 填；数组/函数形参已退化指针） */
    int nargs;
    Type *param_tys[64];
    char *param_names[64];
    bool is_knr;         /* K&R 旧式（参数类型未声明/未知） */
    bool is_variadic;    /* 含 "..." */
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
    ND_ASSIGN,   /* lhs = 可寻址表达式，rhs = 表达式（M2 泛化：ND_VAR/GVAR/DEREF/MEMBER） */
    ND_EQ,       /* == */
    ND_NE,       /* != */
    ND_LT,       /* <  有符号/无符号按 is_unsigned */
    ND_LE,       /* <= */
    ND_GT,       /* >  */
    ND_GE,       /* >= */
    ND_LOGAND,   /* && 短路 */
    ND_LOGOR,    /* || 短路 */
    ND_NOT,      /* !  一元 */
    ND_RETURN,   /* 语句：lhs = 返回值表达式 */
    ND_IF,       /* lhs = 条件，rhs = then，els = else（可空） */
    ND_WHILE,    /* lhs = 条件，rhs = 循环体 */
    ND_FOR,      /* lhs = init（可空），rhs = 条件（可空），els = inc（可空），body = 循环体 */
    ND_GVAR,     /* 全局变量：name = 全局名（label 即名字） */
    ND_FUNC,     /* 函数名表达式：name = 函数名（结果 = 函数地址） */
    ND_CALL,     /* 函数调用：name = 函数名（NULL = 动态调用，lhs = 函数指针表达式），rhs = 实参链表，val = 实参数 */
    ND_STR,      /* 字符串字面量：val = 字符串编号（数据段 label s%d） */
    ND_ADDR,     /* 一元 &：lhs = 变量（结果 = 地址） */
    ND_DEREF,    /* 一元 *：lhs = 指针表达式（结果 = 指向值） */
    ND_MEMBER,   /* 成员访问：lhs = 聚合对象（求地址），val = 成员字节偏移；bit_offset/bit_width 位域 */
    ND_CAST,     /* 类型转换：lhs = 表达式，ty = 目标类型 */
    ND_BITNOT,   /* 一元 ~ */
    ND_BITAND,   /* & */
    ND_BITOR,    /* | */
    ND_BITXOR,   /* ^ */
    ND_LSL,      /* << */
    ND_LSR,      /* >>（结果类型 is_unsigned → lsr，否则 asr） */
    ND_COND,     /* ?: lhs=条件 rhs=then els=else（值 = 所选臂） */
    ND_COMMA,    /* 逗号：lhs 先求值丢弃，值 = rhs */
    ND_LABEL,    /* 语句：num = 标签编号，rhs = 后续语句 */
    ND_GOTO,     /* 语句：num = 标签编号 */
    ND_SWITCH,   /* 语句：lhs=条件 rhs=case 链（ND_CASE/ND_DEFAULT）els=体，offset=隐藏条件槽 */
    ND_CASE,     /* 语句/链节点：val=case 值，num=标签编号；case 后的语句在 body 链中顺序生成 */
    ND_DEFAULT,  /* 同上（无值） */
    ND_BREAK,    /* 语句：jmp 最内层循环/switch 出口 */
    ND_CONTINUE, /* 语句：jmp 最内层循环条件/增量处 */
    ND_DOWHILE,  /* 语句：lhs=体，rhs=条件 */
    ND_VASTART,  /* 表达式：__builtin_va_start 展开；lhs = AP lvalue */
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
    int64_t val;   /* ND_NUM / ND_CALL 实参数 / ND_STR 编号 / ND_MEMBER 字节偏移 */
    int offset;    /* ND_VAR：相对 sp 的负偏移；ND_SWITCH：隐藏条件槽 */
    int num;       /* ND_LABEL / ND_GOTO / ND_CASE / ND_DEFAULT：标签编号 */
    int bit_offset;  /* ND_MEMBER 位域（-1 = 非位域） */
    int bit_width;   /* ND_MEMBER 位域宽度（-1 = 非位域） */
    char *name;    /* ND_GVAR / ND_CALL：标识符名（malloc；ND_CALL 动态调用为 NULL） */
} Node;

/* 函数定义/原型 */
typedef struct Func {
    struct Func *next;
    char *name;
    int len;
    Type *ret_ty;    /* 返回类型（TY_VOID = void） */
    Type *fty;       /* TY_FUNC 类型（函数名表达式/函数指针用） */
    int nargs;       /* 参数个数 */
    Type *param_tys[64];    /* 参数类型（数组形参已退化指针） */
    char *param_names[64];  /* 参数名（定义时；原型可为 NULL） */
    int frame_size;  /* 帧大小（局部变量 + 参数栈槽） */
    Node *body;      /* 语句链表（NULL = 仅原型） */
    bool is_knr;       /* K&R 旧式（参数未声明/未知；调用不检查个数） */
    bool is_variadic;  /* 原型含 "..." */
    bool is_decl;      /* 仅原型声明（可被定义覆盖） */
    bool is_static;    /* static 函数（不导出符号，Task 5 消费） */
    bool has_retbuf;   /* struct/union 返回：隐藏首参数 = 返回缓冲区指针 */
} Func;

/* 全局变量声明 */
typedef struct Global {
    struct Global *next;
    char *name;
    int len;
    Type *ty;
    unsigned char *init_data;  /* 初始化字节（大端布局）；NULL = 未初始化 */
    int init_data_len;
    bool is_static;    /* 不导出符号（Task 5 消费） */
    bool is_extern;    /* extern 声明：引用外部定义，不分配数据 */
    /* 初始化器中的字符串引用（4 字节槽 → 数据段字符串 label @s%d）。
     * 交错数组：str_relocs[2k] = 槽偏移（4 对齐），str_relocs[2k+1] = 字符串编号 */
    int *str_relocs;
    int n_str_relocs;  /* reloc 对数 */
    /* 初始化器中的函数地址引用（4 字节槽 → 函数 label @name）。 */
    char *func_reloc_names[64];   /* 函数名 */
    int func_reloc_offsets[64];   /* 槽偏移（4 对齐） */
    int n_func_relocs;            /* reloc 数 */
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

/* 生成可重定位对象（符号引用形态：@name D16 / @hi:@lo D32 / call J16）；
 * d32 = 数据引用拆 32 位（mov @hi + lsl + or @lo）。布局由链接器完成 */
bool codegen(Program *prog, Obj *obj, bool d32);

/* ---------- 编译入口（main.c 与测试运行器共用） ---------- */

/* 编译 C 源文本 src 到内存可重定位对象（多文件链接用）；
 * d32 = 数据引用拆 32 位装载（Task 6 起）。失败返回 false。 */
bool symcc_compile_obj(const char *src, Obj *obj, bool d32);

/* 全链路：编译 → 链接（runtime/crt0.asm）→ 绝对 asm 文本到 out。
 * 失败（含超出 16 位立即数/未定义符号）返回 false。
 * 词法/语法错误直接退出进程（工具型编译器，M1 从简）。 */
bool symcc_compile_text(const char *src, FILE *out);

#endif /* SYMCC_H */
