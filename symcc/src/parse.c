/* symcc/src/parse.c — 递归下降语法分析（M2 Task 2 类型系统）
 *
 * C89 子集文法：
 *   program   = (typedef | struct/union/enum | funcdef | prototype | global)*
 *   funcdef   = declspec declarator "{" stmt* "}"      （declarator 含参数列表）
 *   prototype = declspec declarator ";"
 *   global    = declspec declarator ("=" init)? (";" | "," ...)*
 *   init      = const-expr | string | "{" init-list "}"
 *   stmt      = "return" expr ";" | "{" stmt* "}" | decl ";" | expr ";"
 *             | "if" "(" expr ")" stmt ("else" stmt)?
 *             | "while" "(" expr ")" stmt | "for" "(" expr? ";" expr? ";" expr? ")" stmt
 *   expr      = assign
 *   assign    = logor ("=" assign)?
 *   unary     = ("-" | "!" | "~" | "&" | "*" | sizeof | "(" type ")") unary | postfix
 *   postfix   = primary ( "[" expr "]" | "." ident | "->" ident | "(" args ")" )*
 *   primary   = num | "(" expr ")" | ident | 字符串字面量 | ident "(" args ")" | ident
 *
 * 类型系统：typedef 表 / struct-union-enum tag 表 / 枚举常量表；数组（下标展开为
 * 指针算术）、结构体成员（ND_MEMBER，含位域）、sizeof（不求值）、cast、
 * 函数指针（动态调用展开为 counter/add/push/jmp 序列）。
 *
 * 布局（大端）：struct 成员 4 对齐；位域从 4 字节单元最高位起打包。
 * 局部变量栈槽按 align4(size) 分配（char 仍 4 字节槽）。
 * 函数调用：被调方入口 sp 指向返回地址，实参 k 在 [sp+4+4k]，序言拷入栈槽。
 *
 * 函数注册：pre_scan 浅扫描预注册 int/char/unsigned/void 开头的函数定义
 * （不解析类型，仅 ident "(" 模式匹配 + 跳过函数体），使调用可前向/互递归；
 * 其余类型开头的函数（如返回 struct）依赖隐式声明（C89）或定义顺序。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"

static Token *tok;

/* 类型单例 */
Type *ty_int, *ty_char, *ty_void;

Type *ty_ptr(Type *base) {
    Type *t = (Type *)calloc(1, sizeof(Type));
    if (!t) { fprintf(stderr, "out of memory\n"); exit(1); }
    t->kind = TY_PTR;
    t->size = 4;
    t->base = base;
    return t;
}

static Type *ty_uint(void) {
    static Type t;
    t = *ty_int;
    t.is_unsigned = true;
    return &t;
}

static Type *ty_array(Type *base, int64_t len) {
    Type *t = (Type *)calloc(1, sizeof(Type));
    if (!t) { fprintf(stderr, "out of memory\n"); exit(1); }
    t->kind = TY_ARRAY;
    t->base = base;
    t->array_len = len;
    t->size = len < 0 ? 0 : (int)(len * (int64_t)base->size);
    return t;
}

/* 函数类型 → 函数指针（变量/成员/typedef 声明处使用：
 * `int (*fp)(int)` 的 declarator 结果为 TY_FUNC，作为变量它是指向函数的指针） */
static Type *func_to_ptr(Type *ty) {
    if (ty->kind == TY_FUNC)
        return ty_ptr(ty);
    return ty;
}

/* 局部变量符号表（链表，每个函数独立；无作用域回收——M1 行为） */
typedef struct Var {
    struct Var *next;
    char *name;
    int len;
    int offset;      /* 相对 sp 的负偏移 */
    Type *ty;
} Var;

static Var *vars;
static int locals_bytes;   /* 当前函数帧大小累计 */

/* typedef 表 / 枚举常量表 / struct-union-enum tag 表 */
typedef struct TDef {
    struct TDef *next;
    char *name;
    int len;
    Type *ty;
} TDef;

typedef struct EnumConst {
    struct EnumConst *next;
    char *name;
    int len;
    int64_t val;
} EnumConst;

typedef struct Tag {
    struct Tag *next;
    char *name;
    int len;
    Type *ty;
    int tag_kind;    /* 0=struct 1=union 2=enum */
} Tag;

static TDef *tdefs;
static EnumConst *enums;
static Tag *tags;

/* 全局变量表与函数表（定义顺序） */
static Global *globals;
static Func *funcs;

/* 字符串字面量编号与收集 */
static int nstrings;
static Token *str_head, **str_tail = &str_head;

/* 声明修饰标志（declspec 置位，调用方消费后复位） */
static bool decl_is_typedef, decl_is_static, decl_is_extern;

/* ---- Task 3：语句级状态（函数级，funcdef 重置） ---- */

/* goto/标签表：名字 → 编号（Lg%d）。goto 前向引用先注册，label 定义置 defined */
typedef struct LblEnt { struct LblEnt *next; char *name; int len; int num; bool defined; } LblEnt;
static LblEnt *lbls;
static int lbl_num;          /* 标签编号计数器（goto/label/case 共用 Lg%d 命名空间） */

/* break/continue 合法性：breakable = 循环+switch，loops = 仅循环 */
static int breakable_depth, loop_depth;

/* switch 的 case 收集器栈（嵌套 switch 各占一层；stmt 的 case 注册到栈顶） */
static Node *case_stack[32];
static int case_depth;

/* 类型解析前向声明（eval_unary 的 sizeof/cast 用） */
static Type *declspec(void);
static Type *declarator(Type *base, Token **namep);

static void error_at(Token *t, const char *msg) {
    fprintf(stderr, "parse error at \"%.*s\": %s\n", t->len, t->loc, msg);
    exit(1);
}

static Token *skip(const char *s) {
    if (!tok_is(tok, s))
        error_at(tok, s);
    return tok = tok->next;
}

static Node *new_node(int kind, Token *t) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (!n) { fprintf(stderr, "out of memory\n"); exit(1); }
    n->kind = kind;
    n->tok = t;
    n->bit_offset = -1;
    n->bit_width = -1;
    return n;
}

static Node *new_num(int64_t v, Token *t) {
    Node *n = new_node(ND_NUM, t);
    n->val = v;
    n->ty = ty_int;
    return n;
}

static Node *new_binary(int kind, Node *lhs, Node *rhs, Token *t) {
    Node *n = new_node(kind, t);
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}

static char *xstrndup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* 深拷贝表达式子树（复合赋值/++-- 的 lvalue 双份展开用）。
 * 表达式节点 next 恒为 NULL（链只属于语句列表），不复制。 */
static Node *clone_node(Node *n) {
    if (!n)
        return NULL;
    Node *c = (Node *)malloc(sizeof(Node));
    if (!c) { fprintf(stderr, "out of memory\n"); exit(1); }
    *c = *n;
    c->lhs = clone_node(n->lhs);
    c->rhs = clone_node(n->rhs);
    c->els = clone_node(n->els);
    c->body = clone_node(n->body);
    return c;
}

/* 隐藏临时局部变量（后缀 ++/--、switch 条件）：分配 4 字节帧槽，
 * 返回 ND_VAR 节点（ty 由调用方设置） */
static Node *hidden_lvar(Type *ty) {
    locals_bytes += 4;
    Node *v = new_node(ND_VAR, NULL);
    v->offset = -locals_bytes;
    v->ty = ty;
    return v;
}

/* 标签名 → 编号（goto 前向引用也先注册）；定义时置 defined */
static int label_of(Token *t) {
    for (LblEnt *e = lbls; e; e = e->next)
        if (e->len == t->len && strncmp(e->name, t->loc, (size_t)t->len) == 0)
            return e->num;
    LblEnt *e = (LblEnt *)calloc(1, sizeof(LblEnt));
    if (!e) { fprintf(stderr, "out of memory\n"); exit(1); }
    e->name = xstrndup(t->loc, (size_t)t->len);
    e->len = t->len;
    e->num = lbl_num++;
    e->next = lbls;
    lbls = e;
    return e->num;
}

/* case/default 注册到当前 switch 收集器（检查重复 case 值） */
static void register_case(Node *n, Token *t) {
    if (case_depth == 0)
        error_at(t, "case outside switch");
    Node *list = case_stack[case_depth - 1];
    if (n->kind == ND_CASE) {
        for (Node *c = list; c; c = c->next)
            if (c->kind == ND_CASE && c->val == n->val)
                error_at(t, "duplicate case value");
    } else {
        for (Node *c = list; c; c = c->next)
            if (c->kind == ND_DEFAULT)
                error_at(t, "duplicate default");
    }
    Node *head = case_stack[case_depth - 1];
    if (head) {
        Node *cur = head;
        while (cur->next) cur = cur->next;
        cur->next = n;
    } else {
        case_stack[case_depth - 1] = n;
    }
}

/* ---------- 名称表 ---------- */

static TDef *find_tdef(Token *t) {
    for (TDef *d = tdefs; d; d = d->next)
        if (d->len == t->len && strncmp(d->name, t->loc, (size_t)t->len) == 0)
            return d;
    return NULL;
}

static EnumConst *find_enum(Token *t) {
    for (EnumConst *e = enums; e; e = e->next)
        if (e->len == t->len && strncmp(e->name, t->loc, (size_t)t->len) == 0)
            return e;
    return NULL;
}

static Tag *find_tag(Token *t, int tag_kind) {
    for (Tag *g = tags; g; g = g->next) {
        if (g->len == t->len && strncmp(g->name, t->loc, (size_t)t->len) == 0) {
            if (g->tag_kind != tag_kind)
                error_at(t, "tag name used for different kind of type");
            return g;
        }
    }
    return NULL;
}

static void add_tdef(Token *t, Type *ty) {
    TDef *d = (TDef *)calloc(1, sizeof(TDef));
    if (!d) { fprintf(stderr, "out of memory\n"); exit(1); }
    d->name = xstrndup(t->loc, (size_t)t->len);
    d->len = t->len;
    d->ty = ty;
    d->next = tdefs;
    tdefs = d;    /* 重复定义覆盖（宽松，幂等） */
}

static void add_enum(Token *t, int64_t val) {
    EnumConst *e = (EnumConst *)calloc(1, sizeof(EnumConst));
    if (!e) { fprintf(stderr, "out of memory\n"); exit(1); }
    e->name = xstrndup(t->loc, (size_t)t->len);
    e->len = t->len;
    e->val = val;
    e->next = enums;
    enums = e;    /* 重复覆盖 */
}

static void add_tag(Token *t, Type *ty, int tag_kind) {
    Tag *g = (Tag *)calloc(1, sizeof(Tag));
    if (!g) { fprintf(stderr, "out of memory\n"); exit(1); }
    g->name = xstrndup(t->loc, (size_t)t->len);
    g->len = t->len;
    g->ty = ty;
    g->tag_kind = tag_kind;
    g->next = tags;
    tags = g;
}

static Func *find_func(Token *t) {
    for (Func *f = funcs; f; f = f->next)
        if (f->len == t->len && strncmp(f->name, t->loc, (size_t)t->len) == 0)
            return f;
    return NULL;
}

static Global *find_global(Token *t) {
    for (Global *g = globals; g; g = g->next)
        if (g->len == t->len && strncmp(g->name, t->loc, (size_t)t->len) == 0)
            return g;
    return NULL;
}

static Member *find_member(Type *ty, Token *t) {
    for (Member *m = ty->members; m; m = m->next)
        if (m->len == t->len && strncmp(m->name, t->loc, (size_t)m->len) == 0)
            return m;
    error_at(t, "no such member");
    return NULL;
}

/* ---------- 类型关键字判定 ---------- */

static bool is_typename(Token *t) {
    if (t->kind != TK_KEYWORD)
        return false;
    return tok_is(t, "int") || tok_is(t, "char") || tok_is(t, "void") ||
           tok_is(t, "unsigned") || tok_is(t, "struct") || tok_is(t, "union") ||
           tok_is(t, "enum") || tok_is(t, "typedef") || tok_is(t, "static") ||
           tok_is(t, "extern") || tok_is(t, "const") || tok_is(t, "volatile") ||
           tok_is(t, "register") || tok_is(t, "inline");
}

/* 语句/顶层声明起点：类型关键字或 typedef 名 */
static bool is_declspec_start(Token *t) {
    return is_typename(t) || (t->kind == TK_IDENT && find_tdef(t));
}

/* ---------- 常量表达式求值（初始化器/数组长度/位域宽度） ---------- */

static Node *unary(void);   /* sizeof 表达式形式用 */
static bool is_lvalue(Node *n);   /* ++/-- 与赋值目标检查 */
static Node *new_assign(Node *lhs, Node *rhs, Token *t);  /* 后缀 ++/-- 用 */
static Node *assign(void);   /* 复合赋值 rhs 用 */
static char *xstrndup(const char *s, size_t n);  /* label_of 用 */

static int64_t eval_const(Token **rest, Token *t);   /* 主入口 */
static int64_t eval_ternary(Token **cur);
static int64_t eval_logor(Token **cur);
static int64_t eval_logand(Token **cur);
static int64_t eval_bitor(Token **cur);
static int64_t eval_bitxor(Token **cur);
static int64_t eval_bitand(Token **cur);
static int64_t eval_equality(Token **cur);
static int64_t eval_rel(Token **cur);
static int64_t eval_shift(Token **cur);
static int64_t eval_add(Token **cur);
static int64_t eval_mul(Token **cur);
static int64_t eval_unary(Token **cur);
static int64_t eval_primary(Token **cur);

static int64_t eval_const(Token **rest, Token *t) {
    Token *cur = t;
    int64_t v = eval_ternary(&cur);
    *rest = cur;
    return v;
}

static int64_t eval_ternary(Token **cur) {
    int64_t c = eval_logor(cur);
    if (tok_is(*cur, "?")) {
        *cur = (*cur)->next;
        int64_t a = eval_ternary(cur);
        if (!tok_is(*cur, ":"))
            error_at(*cur, "expected ':' in conditional expression");
        *cur = (*cur)->next;
        int64_t b = eval_ternary(cur);
        return c ? a : b;
    }
    return c;
}

static int64_t eval_logor(Token **cur) {
    int64_t a = eval_logand(cur);
    while (tok_is(*cur, "||")) {
        *cur = (*cur)->next;
        int64_t b = eval_logand(cur);
        a = (a != 0) || (b != 0);
    }
    return a;
}

static int64_t eval_logand(Token **cur) {
    int64_t a = eval_bitor(cur);
    while (tok_is(*cur, "&&")) {
        *cur = (*cur)->next;
        int64_t b = eval_bitor(cur);
        a = (a != 0) && (b != 0);
    }
    return a;
}

static int64_t eval_bitor(Token **cur) {
    int64_t a = eval_bitxor(cur);
    while (tok_is(*cur, "|")) {
        *cur = (*cur)->next;
        a |= eval_bitxor(cur);
    }
    return a;
}

static int64_t eval_bitxor(Token **cur) {
    int64_t a = eval_bitand(cur);
    while (tok_is(*cur, "^")) {
        *cur = (*cur)->next;
        a ^= eval_bitand(cur);
    }
    return a;
}

static int64_t eval_bitand(Token **cur) {
    int64_t a = eval_equality(cur);
    while (tok_is(*cur, "&")) {
        *cur = (*cur)->next;
        a &= eval_equality(cur);
    }
    return a;
}

static int64_t eval_equality(Token **cur) {
    int64_t a = eval_rel(cur);
    for (;;) {
        if (tok_is(*cur, "==")) {
            *cur = (*cur)->next;
            a = (a == eval_rel(cur));
        } else if (tok_is(*cur, "!=")) {
            *cur = (*cur)->next;
            a = (a != eval_rel(cur));
        } else {
            return a;
        }
    }
}

static int64_t eval_rel(Token **cur) {
    int64_t a = eval_shift(cur);
    for (;;) {
        if (tok_is(*cur, "<")) {
            *cur = (*cur)->next;
            a = (a < eval_shift(cur));
        } else if (tok_is(*cur, "<=")) {
            *cur = (*cur)->next;
            a = (a <= eval_shift(cur));
        } else if (tok_is(*cur, ">")) {
            *cur = (*cur)->next;
            a = (a > eval_shift(cur));
        } else if (tok_is(*cur, ">=")) {
            *cur = (*cur)->next;
            a = (a >= eval_shift(cur));
        } else {
            return a;
        }
    }
}

static int64_t eval_shift(Token **cur) {
    int64_t a = eval_add(cur);
    for (;;) {
        if (tok_is(*cur, "<<")) {
            *cur = (*cur)->next;
            a = (a << eval_add(cur));
        } else if (tok_is(*cur, ">>")) {
            *cur = (*cur)->next;
            a = (a >> eval_add(cur));
        } else {
            return a;
        }
    }
}

static int64_t eval_add(Token **cur) {
    int64_t a = eval_mul(cur);
    for (;;) {
        if (tok_is(*cur, "+")) {
            *cur = (*cur)->next;
            a += eval_mul(cur);
        } else if (tok_is(*cur, "-")) {
            *cur = (*cur)->next;
            a -= eval_mul(cur);
        } else {
            return a;
        }
    }
}

static int64_t eval_mul(Token **cur) {
    int64_t a = eval_unary(cur);
    for (;;) {
        if (tok_is(*cur, "*")) {
            *cur = (*cur)->next;
            a *= eval_unary(cur);
        } else if (tok_is(*cur, "/")) {
            *cur = (*cur)->next;
            int64_t b = eval_unary(cur);
            a = b ? a / b : 0;
        } else if (tok_is(*cur, "%")) {
            *cur = (*cur)->next;
            int64_t b = eval_unary(cur);
            a = b ? a % b : 0;
        } else {
            return a;
        }
    }
}

static int64_t eval_unary(Token **cur) {
    Token *t = *cur;
    if (tok_is(t, "+")) {
        *cur = t->next;
        return eval_unary(cur);
    }
    if (tok_is(t, "-")) {
        *cur = t->next;
        return -eval_unary(cur);
    }
    if (tok_is(t, "!")) {
        *cur = t->next;
        return !eval_unary(cur);
    }
    if (tok_is(t, "~")) {
        *cur = t->next;
        return ~eval_unary(cur);
    }
    if (tok_is_kw(t, "sizeof")) {
        /* sizeof(type) 或 sizeof expr（不求值）。declspec 用全局 tok →
         * 临时切换，取回后恢复。 */
        *cur = t->next;
        if (tok_is(*cur, "(") && is_declspec_start((*cur)->next)) {
            *cur = (*cur)->next;
            Token *save = tok;
            tok = *cur;
            Type *bt = declspec();
            Token *nm = NULL;
            Type *ty = declarator(bt, &nm);
            *cur = tok;
            tok = save;
            if (!tok_is(*cur, ")"))
                error_at(*cur, "expected ')' in sizeof");
            *cur = (*cur)->next;
            return ty->size;
        }
        /* sizeof 表达式：不求值，只取类型（解析会建节点，无副作用） */
        Token *save = tok;
        tok = *cur;
        Node *e = unary();
        *cur = tok;
        tok = save;
        return e->ty->size;
    }
    return eval_primary(cur);
}

static int64_t eval_primary(Token **cur) {
    Token *t = *cur;
    if (t->kind == TK_NUM) {
        *cur = t->next;
        return t->val;
    }
    if (tok_is(t, "(")) {
        *cur = t->next;
        int64_t v = eval_ternary(cur);
        if (!tok_is(*cur, ")"))
            error_at(*cur, "expected ')' in constant expression");
        *cur = (*cur)->next;
        return v;
    }
    if (t->kind == TK_IDENT) {
        EnumConst *e = find_enum(t);
        if (e) {
            *cur = t->next;
            return e->val;
        }
        error_at(t, "undefined identifier in constant expression");
    }
    error_at(t, "expected constant expression");
    return 0;
}

/* ---------- 类型声明符 ---------- */

static Type *declspec(void);
static Type *declarator(Type *base, Token **namep);
static Type *suffix(Type *base, Token **namep);
static Type *struct_decl(bool is_struct);
static Type *enum_decl(void);
static void struct_members(Type *ty);

/* declspec = 修饰符* 类型说明符
 * 返回类型；标志（typedef/static/extern）写入全局，调用方消费。 */
static Type *declspec(void) {
    decl_is_typedef = decl_is_static = decl_is_extern = false;
    bool is_unsigned = false;
    Type *base = NULL;

    for (;;) {
        if (tok_is_kw(tok, "typedef")) {
            decl_is_typedef = true;
            tok = tok->next;
            continue;
        }
        if (tok_is_kw(tok, "static")) {
            decl_is_static = true;
            tok = tok->next;
            continue;
        }
        if (tok_is_kw(tok, "extern")) {
            decl_is_extern = true;
            tok = tok->next;
            continue;
        }
        if (tok_is_kw(tok, "const") || tok_is_kw(tok, "volatile") ||
            tok_is_kw(tok, "register") || tok_is_kw(tok, "inline")) {
            tok = tok->next;
            continue;
        }
        if (tok_is_kw(tok, "unsigned")) {
            is_unsigned = true;
            tok = tok->next;
            continue;
        }
        if (tok_is_kw(tok, "void")) {
            if (base || is_unsigned)
                error_at(tok, "invalid type specifier combination");
            base = ty_void;
            tok = tok->next;
            continue;
        }
        if (tok_is_kw(tok, "int")) {
            if (base)
                error_at(tok, "duplicate type specifier");
            base = ty_int;
            tok = tok->next;
            continue;
        }
        if (tok_is_kw(tok, "char")) {
            if (base)
                error_at(tok, "duplicate type specifier");
            base = ty_char;
            tok = tok->next;
            continue;
        }
        if (tok_is_kw(tok, "struct")) {
            if (base)
                error_at(tok, "duplicate type specifier");
            base = struct_decl(true);
            continue;
        }
        if (tok_is_kw(tok, "union")) {
            if (base)
                error_at(tok, "duplicate type specifier");
            base = struct_decl(false);
            continue;
        }
        if (tok_is_kw(tok, "enum")) {
            if (base)
                error_at(tok, "duplicate type specifier");
            base = enum_decl();
            continue;
        }
        if (tok->kind == TK_IDENT) {
            TDef *d = find_tdef(tok);
            if (d) {
                if (base)
                    error_at(tok, "invalid type specifier combination");
                base = d->ty;
                tok = tok->next;
                continue;
            }
        }
        break;
    }

    if (base && is_unsigned) {
        if (base->kind != TY_INT && base->kind != TY_CHAR)
            error_at(tok, "'unsigned' applied to non-integer type");
        Type *t = (Type *)calloc(1, sizeof(Type));
        if (!t) { fprintf(stderr, "out of memory\n"); exit(1); }
        *t = *base;
        t->is_unsigned = true;
        return t;              /* 独立副本：不污染单例 */
    }
    if (base)
        return base;
    if (is_unsigned)
        return ty_uint();      /* unsigned 后无类型 → unsigned int */
    /* 仅有存储类/无类型 → 隐式 int（C89） */
    if (decl_is_typedef || decl_is_static || decl_is_extern)
        return ty_int;
    error_at(tok, "expected type specifier");
    return NULL;
}

/* declarator = ( "*"* | "(" declarator ")" )* ident 后缀*
 * abstract（cast/sizeof）：名字可省略。 */
static Type *declarator(Type *base, Token **namep) {
    if (namep)
        *namep = NULL;
    if (tok_is(tok, "(")) {
        tok = tok->next;
        Type *inner = declarator(base, namep);
        skip(")");
        return suffix(inner, namep);
    }
    while (tok_is(tok, "*")) {
        base = ty_ptr(base);
        tok = tok->next;
    }
    if (tok->kind == TK_IDENT) {
        *namep = tok;
        tok = tok->next;
    }
    return suffix(base, namep);
}

/* 后缀：数组 [n] 与函数 (params)。函数定义/原型/函数指针共用。 */
static Type *suffix(Type *base, Token **namep) {
    if (tok_is(tok, "[")) {
        tok = tok->next;
        int64_t len = -1;
        if (!tok_is(tok, "]"))
            len = eval_const(&tok, tok);
        skip("]");
        Type *arr = ty_array(base, len);
        if (base->kind == TY_FUNC)
            error_at(tok, "array of functions");
        return suffix(arr, namep);
    }
    if (tok_is(tok, "(")) {
        /* 函数类型：解析参数（ANSI 或 K&R 名字列表） */
        tok = tok->next;
        Type *ft = (Type *)calloc(1, sizeof(Type));
        if (!ft) { fprintf(stderr, "out of memory\n"); exit(1); }
        ft->kind = TY_FUNC;
        ft->base = base;
        int n = 0;
        if (tok_is(tok, ")")) {
            /* f()：K&R 空参（未声明） */
            ft->is_knr = true;
            tok = tok->next;
        } else if (tok_is_kw(tok, "void") && tok->next && tok_is(tok->next, ")")) {
            tok = tok->next;   /* 吃 void */
            tok = tok->next;   /* 吃 ) */
        } else if (tok->kind == TK_IDENT && !is_typename(tok)) {
            /* K&R 参数名列表 */
            ft->is_knr = true;
            while (tok->kind == TK_IDENT) {
                if (n >= 64)
                    error_at(tok, "too many parameters");
                ft->param_names[n] = xstrndup(tok->loc, (size_t)tok->len);
                n++;
                tok = tok->next;
                if (tok_is(tok, ","))
                    tok = tok->next;
            }
            if (!tok_is(tok, ")"))
                error_at(tok, "expected ')' in K&R parameter list");
            tok = tok->next;
        } else {
            /* ANSI 参数列表 */
            while (!tok_is(tok, ")")) {
                if (tok->kind == TK_EOF)
                    error_at(tok, "unclosed '('");
                if (tok_is(tok, "...")) {
                    ft->is_variadic = true;
                    tok = tok->next;
                    break;
                }
                if (n >= 64)
                    error_at(tok, "too many parameters");
                Token *ptok = tok;
                Type *bt = declspec();
                Token *pname = NULL;
                Type *pt = declarator(bt, &pname);
                if (pt->kind == TY_ARRAY)
                    pt = ty_ptr(pt->base);        /* 数组形参退化指针 */
                if (pt->kind == TY_FUNC)
                    pt = ty_ptr(pt);              /* 函数形参退化指针 */
                ft->param_tys[n] = pt;
                ft->param_names[n] = pname ? xstrndup(pname->loc, (size_t)pname->len) : NULL;
                n++;
                if (tok_is(tok, ",")) {
                    tok = tok->next;
                    continue;
                }
                if (tok_is(tok, ")"))
                    break;
                error_at(ptok, "expected ',' or ')' in parameter list");
            }
            tok = tok->next;   /* 吃 ) */
        }
        ft->nargs = n;
        return ft;
    }
    return base;
}

/* struct/union 声明：tag 复用（不完整类型）或定义（成员布局）。
 * 同 tag 重复定义 → 幂等跳过（头文件多次 include 防护）。 */
static Type *struct_decl(bool is_struct) {
    Type *ty = NULL;
    Token *tag_tok = NULL;
    tok = tok->next;
    if (tok->kind == TK_IDENT) {
        tag_tok = tok;
        Tag *g = find_tag(tok, is_struct ? 0 : 1);
        if (g)
            ty = g->ty;
        tok = tok->next;
    }
    if (tok_is(tok, "{")) {
        if (ty && ty->size != 0) {
            /* 重复定义：跳过成员体（幂等） */
            int depth = 0;
            tok = tok->next;
            while (!(tok_is(tok, "}") && depth == 0)) {
                if (tok->kind == TK_EOF)
                    error_at(tok, "unclosed struct/union body");
                if (tok_is(tok, "{"))
                    depth++;
                else if (tok_is(tok, "}"))
                    depth--;
                tok = tok->next;
            }
            tok = tok->next;   /* 吃 } */
            return ty;
        }
        tok = tok->next;
        if (!ty) {
            ty = (Type *)calloc(1, sizeof(Type));
            if (!ty) { fprintf(stderr, "out of memory\n"); exit(1); }
            ty->kind = is_struct ? TY_STRUCT : TY_UNION;
            ty->size = 0;               /* 不完整 */
        }
        struct_members(ty);
        skip("}");
        if (tag_tok && !find_tag(tag_tok, is_struct ? 0 : 1))
            add_tag(tag_tok, ty, is_struct ? 0 : 1);
    } else if (!ty) {
        error_at(tok, "incomplete struct/union");
    }
    return ty;
}

/* struct_members：成员解析与布局（大端：4 对齐；位域从单元最高位打包） */
static void struct_members(Type *ty) {
    Member head = {0};
    Member *cur = &head;
    int off = 0;
    bool in_bf = false;      /* 当前在位域单元内 */
    int bitpos = 0;          /* 单元内已用位数 */

    while (!tok_is(tok, "}")) {
        if (tok->kind == TK_EOF)
            error_at(tok, "unclosed struct/union body");
        Type *base = declspec();

        /* 无名位域（declspec ":" width） */
        if (tok_is(tok, ":")) {
            tok = tok->next;
            int width = (int)eval_const(&tok, tok);
            if (width == 0) {
                /* :0 强制对齐下一单元 */
                in_bf = false;
                bitpos = 32;
            } else {
                if (!in_bf || bitpos + width > 32) {
                    if (in_bf)
                        off += 4;      /* 越过旧位域单元 */
                    off = (off + 3) & ~3;
                    bitpos = 0;
                    in_bf = true;
                }
                bitpos += width;
            }
            skip(";");
            continue;
        }

        Token *name = NULL;
        Type *mty = func_to_ptr(declarator(base, &name));   /* 函数指针成员 */
        int width = -1;
        if (tok_is(tok, ":")) {
            tok = tok->next;
            width = (int)eval_const(&tok, tok);
        }
        skip(";");
        if (!name)
            error_at(tok, "member name missing");
        if (mty->kind == TY_VOID)
            error_at(name, "member of type void");

        Member *m = (Member *)calloc(1, sizeof(Member));
        if (!m) { fprintf(stderr, "out of memory\n"); exit(1); }
        m->name = xstrndup(name->loc, (size_t)name->len);
        m->len = name->len;
        m->ty = mty;
        m->bit_offset = -1;
        m->bit_width = -1;

        if (width >= 0) {
            if (width == 0) {
                in_bf = false;
                bitpos = 32;           /* 不占成员 */
                continue;
            }
            if (width > 32)
                error_at(name, "bit-field width exceeds 32");
            if (!in_bf || bitpos + width > 32) {
                if (in_bf)
                    off += 4;          /* 越过旧位域单元 */
                off = (off + 3) & ~3;
                bitpos = 0;
                in_bf = true;
            }
            m->offset = off;           /* 单元字节偏移（单元起始） */
            m->bit_offset = bitpos;    /* 单元内最高位起 */
            m->bit_width = width;
            bitpos += width;
            /* 位域单元本身不推进 off：后续位域同单元复用；
             * 非位域/新单元越过（off += 4）。 */
        } else {
            if (in_bf) {
                off += 4;              /* 越过位域单元 */
                in_bf = false;
            }
            off = (off + 3) & ~3;      /* 4 对齐（char 成员也 4 对齐——简化） */
            m->offset = off;
            off += mty->size;
        }
        cur = cur->next = m;
    }
    /* 循环在 "}" 处退出，不消费（调用方 struct_decl skip） */
    if (in_bf)
        off += 4;                  /* 尾部位域单元占 4 字节 */
    ty->members = head.next;
    ty->size = (off + 3) & ~3;   /* 4 对齐总大小 */
}

/* enum 声明：常量注册 + tag（枚举类型 = int） */
static Type *enum_decl(void) {
    Token *t = tok;
    tok = tok->next;
    Token *tag_tok = NULL;
    if (tok->kind == TK_IDENT) {
        tag_tok = tok;
        tok = tok->next;
    }
    if (tok_is(tok, "{")) {
        tok = tok->next;
        int64_t val = 0;
        while (!tok_is(tok, "}")) {
            if (tok->kind == TK_EOF)
                error_at(tok, "unclosed enum body");
            if (tok->kind != TK_IDENT)
                error_at(tok, "expected enum constant name");
            Token *nm = tok;
            tok = tok->next;
            if (tok_is(tok, "=")) {
                tok = tok->next;
                val = eval_const(&tok, tok);
            }
            add_enum(nm, val);
            val++;
            if (tok_is(tok, ","))
                tok = tok->next;
        }
        skip("}");
        if (tag_tok && !find_tag(tag_tok, 2))
            add_tag(tag_tok, ty_int, 2);   /* enum 类型 = int */
        return ty_int;
    }
    if (tag_tok) {
        Tag *g = find_tag(tag_tok, 2);
        if (g)
            return ty_int;
    }
    error_at(t, "incomplete enum");
    return NULL;
}

/* ---------- 表达式 ---------- */

static Node *expr(void);
static Node *stmt(void);

/* 指针缩放：n * elem_size（size==1 时跳过） */
static Node *scale_ptr(Node *n, Type *elem, Token *t) {
    if (elem->size == 1)
        return n;
    return new_binary(ND_MUL, n, new_num(elem->size, t), t);
}

/* 二元运算：默认 int；指针算术与数组退化在此展开 */
static Node *new_binop(int kind, Node *lhs, Node *rhs, Token *t) {
    Node *n = new_binary(kind, lhs, rhs, t);
    n->ty = ty_int;
    if (kind == ND_ADD) {
        /* 数组表达式求值 = 地址（gen 按 ty 分派）；指针 + 整数缩放元素大小 */
        if (lhs->ty->kind == TY_ARRAY) {
            n->lhs = lhs;
            n->rhs = scale_ptr(rhs, lhs->ty->base, t);
            n->ty = ty_ptr(lhs->ty->base);
            return n;
        }
        if (rhs->ty->kind == TY_ARRAY) {
            n->lhs = scale_ptr(lhs, rhs->ty->base, t);
            n->rhs = rhs;
            n->ty = ty_ptr(rhs->ty->base);
            return n;
        }
        if (lhs->ty->kind == TY_PTR && rhs->ty->kind != TY_PTR) {
            Type *elem = lhs->ty->base;
            n->rhs = scale_ptr(rhs, elem, t);
            n->ty = lhs->ty;
            return n;
        }
        if (rhs->ty->kind == TY_PTR && lhs->ty->kind != TY_PTR) {
            Type *elem = rhs->ty->base;
            n->lhs = scale_ptr(lhs, elem, t);
            n->ty = rhs->ty;
            return n;
        }
    }
    if (kind == ND_SUB) {
        if (lhs->ty->kind == TY_ARRAY) {
            /* 数组 - 整数：地址 - 缩放 */
            n->rhs = scale_ptr(rhs, lhs->ty->base, t);
            n->ty = ty_ptr(lhs->ty->base);
            return n;
        }
        if (lhs->ty->kind == TY_PTR && rhs->ty->kind == TY_PTR) {
            /* 指针差：元素数。val 记录元素大小（gen 层除） */
            Type *elem = lhs->ty->base;
            n->val = (elem->kind == TY_VOID || elem->size == 0) ? 1 : elem->size;
            return n;
        }
        if (lhs->ty->kind == TY_PTR && rhs->ty->kind != TY_PTR) {
            Type *elem = lhs->ty->base;
            n->rhs = scale_ptr(rhs, elem, t);
            n->ty = lhs->ty;
            return n;
        }
        if (rhs->ty->kind == TY_ARRAY && lhs->ty->kind != TY_ARRAY) {
            n->lhs = scale_ptr(lhs, rhs->ty->base, t);
            n->ty = ty_ptr(rhs->ty->base);
            return n;
        }
    }
    /* 算术/位运算：任一侧 unsigned → 结果 unsigned（C 语义） */
    if ((kind == ND_ADD || kind == ND_SUB || kind == ND_MUL ||
         kind == ND_DIV || kind == ND_MOD ||
         kind == ND_BITAND || kind == ND_BITOR || kind == ND_BITXOR) &&
        (lhs->ty->is_unsigned || rhs->ty->is_unsigned)) {
        n->ty = ty_uint();
        return n;
    }
    if (kind == ND_LSL || kind == ND_LSR) {
        /* 移位结果类型 = 左操作数（codegen 按 is_unsigned 选 asr/lsr） */
        n->ty = lhs->ty;
        return n;
    }
    if ((kind == ND_LT || kind == ND_LE || kind == ND_GT || kind == ND_GE) &&
        (lhs->ty->is_unsigned || rhs->ty->is_unsigned)) {
        /* 无符号比较：codegen 按 ty->is_unsigned 选 jb/ja 族 */
        n->ty = ty_uint();
        return n;
    }
    return n;
}

/* 解析实参列表（tok 在 "(" 上）→ rhs 链表，*nargs = 实参数 */
static Node *parse_call_args(int64_t *nargs) {
    skip("(");
    Node head = {0}, *cur = &head;
    int n = 0;
    if (!tok_is(tok, ")")) {
        cur = cur->next = assign();   /* 实参 = assignment-expression（逗号分隔） */
        n = 1;
        while (tok_is(tok, ",")) {
            tok = tok->next;
            cur = cur->next = assign();
            n++;
        }
    }
    skip(")");
    *nargs = n;
    return head.next;
}

/* ident 是函数指针变量（局部或全局）？返回其值节点（ND_VAR/ND_GVAR），
 * 否则 NULL。用于 fp(...) 动态调用识别。 */
static Node *find_fp_var(Token *t) {
    for (Var *v = vars; v; v = v->next) {
        if (v->len == t->len && strncmp(v->name, t->loc, (size_t)v->len) == 0) {
            if (v->ty->kind == TY_PTR && v->ty->base->kind == TY_FUNC) {
                Node *n = new_node(ND_VAR, t);
                n->offset = v->offset;
                n->ty = v->ty;
                return n;
            }
            return NULL;   /* 同名非函数指针变量 */
        }
    }
    for (Global *g = globals; g; g = g->next) {
        if (g->len == t->len && strncmp(g->name, t->loc, (size_t)g->len) == 0) {
            if (g->ty->kind == TY_PTR && g->ty->base->kind == TY_FUNC) {
                Node *n = new_node(ND_GVAR, t);
                n->name = xstrndup(t->loc, (size_t)t->len);
                n->ty = g->ty;
                return n;
            }
            return NULL;
        }
    }
    return NULL;
}

/* primary = num | "(" expr ")" | ident | 字符串 | 函数调用（静态/动态） */
static Node *primary(void) {
    if (tok->kind == TK_NUM) {
        Node *n = new_node(ND_NUM, tok);
        n->val = tok->val;
        n->ty = tok->is_unsigned ? ty_uint() : ty_int;
        tok = tok->next;
        return n;
    }
    if (tok_is(tok, "(")) {
        tok = tok->next;
        Node *n = expr();
        skip(")");
        return n;
    }
    if (tok->kind == TK_STR) {
        Node *n = new_node(ND_STR, tok);
        n->val = nstrings++;
        n->ty = ty_ptr(ty_char);      /* 字符串 = char* */
        *str_tail = tok;              /* 收集到程序级链表 */
        str_tail = &tok->next;
        tok = tok->next;
        return n;
    }
    if (tok->kind == TK_IDENT) {
        Token *t = tok;
        /* 函数调用：ident "(" */
        if (tok->next && tok_is(tok->next, "(")) {
            Func *f = find_func(t);
            if (f) {
                tok = tok->next;      /* 跳到 ( */
                Node *n = new_node(ND_CALL, t);
                n->name = xstrndup(t->loc, (size_t)t->len);
                n->ty = f->ret_ty;
                n->rhs = parse_call_args(&n->val);
                if (!f->is_decl && !f->is_knr && !f->is_variadic &&
                    f->nargs != (int)n->val)
                    error_at(t, "wrong number of arguments");
                return n;
            }
            /* 函数指针变量调用：fp(...) → 动态调用（name=NULL，lhs = 函数指针值） */
            Node *fpv = find_fp_var(t);
            if (fpv) {
                tok = tok->next;      /* 跳到 ( */
                Node *n = new_node(ND_CALL, t);
                n->lhs = fpv;
                n->ty = fpv->ty->base->base;   /* 返回类型 */
                n->rhs = parse_call_args(&n->val);
                Type *fty = fpv->ty->base;
                if (!fty->is_knr && !fty->is_variadic &&
                    fty->nargs != (int)n->val)
                    error_at(t, "wrong number of arguments");
                return n;
            }
            /* 未注册：隐式声明（C89）int f() */
            Func *nf = (Func *)calloc(1, sizeof(Func));
            if (!nf) { fprintf(stderr, "out of memory\n"); exit(1); }
            nf->name = xstrndup(t->loc, (size_t)t->len);
            nf->len = t->len;
            nf->ret_ty = ty_int;
            nf->is_decl = true;
            nf->is_knr = true;
            nf->next = funcs;
            funcs = nf;
            /* 重新走函数调用路径 */
            return primary();
        }
        /* 函数名作为表达式（函数指针值） */
        Func *f = find_func(t);
        if (f) {
            Node *n = new_node(ND_FUNC, t);
            n->name = xstrndup(t->loc, (size_t)t->len);
            n->ty = ty_ptr(f->fty ? f->fty : ty_int);
            tok = tok->next;
            return n;
        }
        /* 局部变量 */
        for (Var *v = vars; v; v = v->next) {
            if (v->len == tok->len && strncmp(v->name, tok->loc, (size_t)v->len) == 0) {
                Node *n = new_node(ND_VAR, tok);
                n->offset = v->offset;
                n->ty = v->ty;
                tok = tok->next;
                return n;
            }
        }
        /* 全局变量 */
        for (Global *g = globals; g; g = g->next) {
            if (g->len == tok->len && strncmp(g->name, tok->loc, (size_t)g->len) == 0) {
                Node *n = new_node(ND_GVAR, tok);
                n->name = xstrndup(tok->loc, (size_t)tok->len);
                n->ty = g->ty;
                tok = tok->next;
                return n;
            }
        }
        /* 枚举常量 */
        EnumConst *e = find_enum(tok);
        if (e) {
            Node *n = new_num(e->val, tok);
            tok = tok->next;
            return n;
        }
        error_at(tok, "undefined variable");
    }
    error_at(tok, "expected number, '(' or identifier");
    return NULL;
}

/* 函数指针动态调用（游戏无 call reg） */
static Node *new_dyncall(Node *fn, Token *t) {
    Node *n = new_node(ND_CALL, t);
    n->lhs = fn;
    n->name = NULL;
    Type *ft = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;  /* FUNC */
    n->ty = ft->base;              /* 返回类型 */
    n->val = 0;
    skip("(");
    if (!tok_is(tok, ")")) {
        Node head = {0}, *cur = &head;
        cur = cur->next = assign();   /* 实参 = assignment-expression（逗号分隔） */
        n->val = 1;
        while (tok_is(tok, ",")) {
            tok = tok->next;
            cur = cur->next = assign();
            n->val++;
        }
        n->rhs = head.next;
    }
    skip(")");
    return n;
}

/* postfix = primary ( "[" expr "]" | "." ident | "->" ident | "(" args ")" )* */
static Node *postfix(void) {
    Node *n = primary();
    for (;;) {
        Token *t = tok;
        if (tok_is(tok, "[")) {
            /* a[i] = *(a + i*elem) */
            tok = tok->next;
            Node *idx = expr();
            skip("]");
            if (n->ty->kind != TY_ARRAY && n->ty->kind != TY_PTR)
                error_at(t, "subscript of non-array");
            Type *elem = n->ty->base;
            Node *addr = new_binary(ND_ADD, n, scale_ptr(idx, elem, t), t);
            addr->ty = ty_ptr(elem);
            Node *d = new_binary(ND_DEREF, addr, NULL, t);
            d->ty = elem;
            n = d;
            continue;
        }
        if (tok_is(tok, ".")) {
            tok = tok->next;
            if (tok->kind != TK_IDENT)
                error_at(tok, "expected member name");
            Type *agg = n->ty;
            if (agg->kind != TY_STRUCT && agg->kind != TY_UNION)
                error_at(t, "member access of non-struct");
            Member *m = find_member(agg, tok);
            Node *mn = new_node(ND_MEMBER, t);
            mn->lhs = n;
            mn->val = m->offset;
            mn->bit_offset = m->bit_offset;
            mn->bit_width = m->bit_width;
            mn->ty = m->ty;
            tok = tok->next;
            n = mn;
            continue;
        }
        if (tok_is(tok, "->")) {
            tok = tok->next;
            if (tok->kind != TK_IDENT)
                error_at(tok, "expected member name");
            Type *pt = n->ty;
            if (pt->kind != TY_PTR ||
                (pt->base->kind != TY_STRUCT && pt->base->kind != TY_UNION))
                error_at(t, "arrow access of non-struct pointer");
            Member *m = find_member(pt->base, tok);
            Node *d = new_binary(ND_DEREF, n, NULL, t);
            d->ty = pt->base;
            Node *mn = new_node(ND_MEMBER, t);
            mn->lhs = d;
            mn->val = m->offset;
            mn->bit_offset = m->bit_offset;
            mn->bit_width = m->bit_width;
            mn->ty = m->ty;
            tok = tok->next;
            n = mn;
            continue;
        }
        if (tok_is(tok, "(")) {
            /* 动态调用（函数指针） */
            if (n->ty->kind == TY_PTR && n->ty->base->kind == TY_FUNC) {
                n = new_dyncall(n, t);
                continue;
            }
            if (n->ty->kind == TY_FUNC) {
                /* 函数类型表达式（&f 已退化为指针；此处理论不可达） */
                n = new_dyncall(n, t);
                continue;
            }
            error_at(t, "call of non-function");
        }
        if (tok_is(tok, "++") || tok_is(tok, "--")) {
            /* 后缀：t = x; x = x ± 1; 结果 = t（隐藏临时槽）。
             * ND_COMMA 链顺序求值，值类型 = x 的类型 */
            Token *t = tok;
            bool inc = tok_is(tok, "++");
            tok = tok->next;
            if (!is_lvalue(n))
                error_at(t, "increment/decrement of non-lvalue");
            Node *tmp = hidden_lvar(n->ty);
            Node *a1 = new_assign(clone_node(tmp), clone_node(n), t);
            Node *opn = new_binop(inc ? ND_ADD : ND_SUB, clone_node(n),
                                  new_num(1, t), t);
            Node *a2 = new_assign(clone_node(n), opn, t);
            Node *mid = new_binary(ND_COMMA, a1, a2, t);
            mid->ty = a2->ty;
            Node *res = new_binary(ND_COMMA, mid, clone_node(tmp), t);
            res->ty = tmp->ty;
            n = res;
            continue;
        }
        return n;
    }
}

/* unary = ("-" | "!" | "~" | "&" | "*" | sizeof | cast) unary | postfix */
static Node *unary(void) {
    if (tok_is(tok, "-")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binary(ND_NEG, unary(), NULL, t);
        n->ty = ty_int;
        return n;
    }
    if (tok_is(tok, "!")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binary(ND_NOT, unary(), NULL, t);
        n->ty = ty_int;
        return n;
    }
    if (tok_is(tok, "~")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binary(ND_BITNOT, unary(), NULL, t);
        n->ty = ty_int;
        return n;
    }
    if (tok_is(tok, "+")) {
        tok = tok->next;
        return unary();
    }
    if (tok_is(tok, "++") || tok_is(tok, "--")) {
        /* 前缀：x = x ± 1（值 = 新值）。指针步长由 new_binop 缩放 */
        Token *t = tok;
        bool inc = tok_is(tok, "++");
        tok = tok->next;
        Node *x = unary();
        if (!is_lvalue(x))
            error_at(t, "increment/decrement of non-lvalue");
        Node *opn = new_binop(inc ? ND_ADD : ND_SUB, clone_node(x),
                              new_num(1, t), t);
        return new_assign(x, opn, t);
    }
    if (tok_is(tok, "&")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binary(ND_ADDR, unary(), NULL, t);
        if (n->lhs->kind != ND_VAR && n->lhs->kind != ND_GVAR &&
            n->lhs->kind != ND_DEREF && n->lhs->kind != ND_MEMBER &&
            n->lhs->kind != ND_STR && n->lhs->kind != ND_FUNC)
            error_at(t, "invalid operand for '&'");
        if (n->lhs->kind == ND_STR || n->lhs->kind == ND_FUNC) {
            /* &"abc" / &f：求值即地址 */
            return n->lhs;
        }
        n->ty = ty_ptr(n->lhs->ty);
        return n;
    }
    if (tok_is(tok, "*")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binary(ND_DEREF, unary(), NULL, t);
        if (n->lhs->ty->kind == TY_PTR)
            n->ty = n->lhs->ty->base;
        else if (n->lhs->ty->kind == TY_ARRAY)
            n->ty = n->lhs->ty->base;   /* 数组退化为指针（宽松） */
        else
            error_at(t, "dereference of non-pointer");
        return n;
    }
    if (tok_is_kw(tok, "sizeof")) {
        Token *t = tok;
        tok = tok->next;
        Type *ty = NULL;
        if (tok_is(tok, "(") && is_declspec_start(tok->next)) {
            tok = tok->next;   /* 吃 ( */
            Type *base = declspec();
            Token *nm = NULL;
            ty = declarator(base, &nm);
            skip(")");
        } else {
            Node *e = unary();   /* 不求值 */
            ty = e->ty;
        }
        Node *n = new_num(ty->size, t);
        return n;
    }
    if (tok_is(tok, "(") && is_declspec_start(tok->next)) {
        /* cast：(type) unary */
        Token *t = tok;
        tok = tok->next;   /* 吃 ( */
        Type *base = declspec();
        Token *nm = NULL;
        Type *ty = declarator(base, &nm);
        skip(")");
        Node *n = new_binary(ND_CAST, unary(), NULL, t);
        n->ty = ty;
        return n;
    }
    return postfix();
}

/* mul = unary (("*" | "/" | "%") unary)* */
static Node *mul(void) {
    Node *n = unary();
    for (;;) {
        if (tok_is(tok, "*")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_MUL, n, unary(), t);
        } else if (tok_is(tok, "/")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_DIV, n, unary(), t);
        } else if (tok_is(tok, "%")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_MOD, n, unary(), t);
        } else {
            return n;
        }
    }
}

/* additive = mul ("+" mul | "-" mul)* */
static Node *additive(void) {
    Node *n = mul();
    for (;;) {
        if (tok_is(tok, "+")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_ADD, n, mul(), t);
        } else if (tok_is(tok, "-")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_SUB, n, mul(), t);
        } else {
            return n;
        }
    }
}

/* shift = additive (("<<" | ">>") additive)* */
static Node *shift(void) {
    Node *n = additive();
    for (;;) {
        if (tok_is(tok, "<<")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_LSL, n, additive(), t);
        } else if (tok_is(tok, ">>")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_LSR, n, additive(), t);
        } else {
            return n;
        }
    }
}

/* relational = shift (("<" | "<=" | ">" | ">=") shift)* */
static Node *relational(void) {
    Node *n = shift();
    for (;;) {
        if (tok_is(tok, "<")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_LT, n, shift(), t);
        } else if (tok_is(tok, "<=")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_LE, n, shift(), t);
        } else if (tok_is(tok, ">")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_GT, n, shift(), t);
        } else if (tok_is(tok, ">=")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_GE, n, shift(), t);
        } else {
            return n;
        }
    }
}

/* equality = relational (("==" | "!=") relational)* */
static Node *equality(void) {
    Node *n = relational();
    for (;;) {
        if (tok_is(tok, "==")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_EQ, n, relational(), t);
        } else if (tok_is(tok, "!=")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_NE, n, relational(), t);
        } else {
            return n;
        }
    }
}

/* bitand = equality ("&" equality)* */
static Node *bitand(void) {
    Node *n = equality();
    while (tok_is(tok, "&")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binop(ND_BITAND, n, equality(), t);
    }
    return n;
}

/* bitxor = bitand ("^" bitand)* */
static Node *bitxor(void) {
    Node *n = bitand();
    while (tok_is(tok, "^")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binop(ND_BITXOR, n, bitand(), t);
    }
    return n;
}

/* bitor = bitxor ("|" bitxor)* */
static Node *bitor(void) {
    Node *n = bitxor();
    while (tok_is(tok, "|")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binop(ND_BITOR, n, bitxor(), t);
    }
    return n;
}

/* logand = bitor ("&&" bitor)* */
static Node *logand(void) {
    Node *n = bitor();
    while (tok_is(tok, "&&")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binop(ND_LOGAND, n, bitor(), t);
    }
    return n;
}

/* logor = logand ("||" logand)* */
static Node *logor(void) {
    Node *n = logand();
    while (tok_is(tok, "||")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binop(ND_LOGOR, n, logand(), t);
    }
    return n;
}

/* 赋值表达式节点：ty = 目标类型（codegen 的 ND_ASSIGN 依赖） */
static Node *new_assign(Node *lhs, Node *rhs, Token *t) {
    Node *an = new_binary(ND_ASSIGN, lhs, rhs, t);
    an->ty = lhs->ty;
    return an;
}

static bool is_lvalue(Node *n) {
    return n->kind == ND_VAR || n->kind == ND_GVAR ||
           n->kind == ND_DEREF || n->kind == ND_MEMBER;
}

/* lhs op= rhs → lhs = lhs op rhs（lhs 表达式克隆两份；无副作用 lvalue
 * 求两次地址等价，C89 复合赋值要求 lvalue 求值一次，此处宽松接受） */
static Node *expand_compound(Node *lhs, Token *t, int opkind) {
    Node *l1 = clone_node(lhs);
    Node *l2 = clone_node(lhs);
    Node *r = assign();
    Node *opn = new_binop(opkind, l1, r, t);
    return new_assign(l2, opn, t);
}

/* ternary = logor ("?" expr ":" ternary)? —— C89 ':' 后是 conditional */
static Node *ternary(void) {
    Node *cond = logor();
    if (tok_is(tok, "?")) {
        Token *t = tok;
        tok = tok->next;
        Node *then = expr();
        skip(":");
        Node *els = ternary();
        Node *n = new_node(ND_COND, t);
        n->lhs = cond;
        n->rhs = then;
        n->els = els;
        n->ty = then->ty;
        return n;
    }
    return cond;
}

/* assign = ternary (("=" | op "=") assign)? */
static Node *assign(void) {
    Node *n = ternary();
    if (tok_is(tok, "=")) {
        Token *t = tok;
        tok = tok->next;
        if (!is_lvalue(n))
            error_at(t, "assignment target is not addressable");
        n = new_assign(n, assign(), t);
        return n;
    }
    int opkind = 0;
    if (tok_is(tok, "+=")) opkind = ND_ADD;
    else if (tok_is(tok, "-=")) opkind = ND_SUB;
    else if (tok_is(tok, "*=")) opkind = ND_MUL;
    else if (tok_is(tok, "/=")) opkind = ND_DIV;
    else if (tok_is(tok, "%=")) opkind = ND_MOD;
    else if (tok_is(tok, "&=")) opkind = ND_BITAND;
    else if (tok_is(tok, "|=")) opkind = ND_BITOR;
    else if (tok_is(tok, "^=")) opkind = ND_BITXOR;
    else if (tok_is(tok, "<<=")) opkind = ND_LSL;
    else if (tok_is(tok, ">>=")) opkind = ND_LSR;
    if (opkind) {
        Token *t = tok;
        tok = tok->next;
        if (!is_lvalue(n))
            error_at(t, "assignment target is not addressable");
        n = expand_compound(n, t, opkind);
    }
    return n;
}

/* expr = assign ("," assign)* */
static Node *expr(void) {
    Node *n = assign();
    while (tok_is(tok, ",")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binary(ND_COMMA, n, assign(), t);
        n->ty = n->rhs->ty;
    }
    return n;
}

/* ---------- 语句 ---------- */

/* target[i]（target 是数组表达式，求值 = 地址） */
static Node *arr_elem(Node *target, int64_t i, Type *elem, Token *t) {
    Node *idx = (elem->size == 1) ? new_num(i, t)
                                  : new_binary(ND_MUL, new_num(i, t), new_num(elem->size, t), t);
    Node *addr = new_binary(ND_ADD, target, idx, t);
    addr->ty = ty_ptr(elem);
    Node *d = new_binary(ND_DEREF, addr, NULL, t);
    d->ty = elem;
    return d;
}

/* 局部初始化：返回赋值链（多节点链表） */
static Node *init_local(Token **rest, Token *t, Type *ty, Node *target);

static Node *init_local(Token **rest, Token *t, Type *ty, Node *target) {
    if (ty->kind == TY_ARRAY) {
        Node head = {0}, *cur = &head;
        int64_t len = ty->array_len;
        if (t->kind == TK_STR && ty->base->kind == TY_CHAR) {
            /* 字符串初始化：字节 + NUL 置零 */
            if (t->str_len >= len)
                error_at(t, "string too long for array");
            for (int i = 0; i < t->str_len; i++)
                cur = cur->next = new_assign(                                             arr_elem(target, i, ty->base, t),
                                             new_num((unsigned char)t->str[i], t), t);
            for (int64_t i = t->str_len; i < len; i++)
                cur = cur->next = new_assign(                                             arr_elem(target, i, ty->base, t),
                                             new_num(0, t), t);
            *rest = t->next;
            return head.next;
        }
        if (!tok_is(t, "{"))
            error_at(t, "array initializer must be '{' or string");
        tok = t->next;
        int64_t i;
        for (i = 0; i < len; i++) {
            if (tok_is(tok, "}"))
                break;
            if (i > 0)
                skip(",");
            cur = cur->next = init_local(&tok, tok, ty->base,
                                         arr_elem(target, i, ty->base, t));
        }
        if (!tok_is(tok, "}"))
            error_at(tok, "too many initializers");
        skip("}");
        /* 剩余元素补 0 */
        for (; i < len; i++)
            cur = cur->next = new_assign(                                         arr_elem(target, i, ty->base, t),
                                         new_num(0, t), t);
        *rest = tok;
        return head.next;
    }
    if (ty->kind == TY_STRUCT) {
        Node head = {0}, *cur = &head;
        if (!tok_is(t, "{"))
            error_at(t, "struct initializer must be '{'");
        tok = t->next;
        Member *m;
        for (m = ty->members; m; m = m->next) {
            if (tok_is(tok, "}"))
                break;
            if (m != ty->members)
                skip(",");
            Node *mn = new_node(ND_MEMBER, t);
            mn->lhs = target;
            mn->val = m->offset;
            mn->bit_offset = m->bit_offset;
            mn->bit_width = m->bit_width;
            mn->ty = m->ty;
            cur = cur->next = init_local(&tok, tok, m->ty, mn);
        }
        if (!tok_is(tok, "}"))
            error_at(tok, "too many initializers");
        skip("}");
        /* 剩余成员补 0 */
        for (; m; m = m->next) {
            Node *mn = new_node(ND_MEMBER, t);
            mn->lhs = target;
            mn->val = m->offset;
            mn->bit_offset = m->bit_offset;
            mn->bit_width = m->bit_width;
            mn->ty = m->ty;
            cur = cur->next = new_assign(mn, new_num(0, t), t);
        }
        *rest = tok;
        return head.next;
    }
    if (ty->kind == TY_UNION) {
        if (!tok_is(t, "{"))
            error_at(t, "union initializer must be '{'");
        tok = t->next;
        Node *n = NULL;
        if (ty->members)
            n = init_local(&tok, tok, ty->members->ty, target);
        skip("}");
        *rest = tok;
        return n;
    }
    /* 标量：初始化器 = assignment-expression（花括号逗号是分隔符） */
    Node *n = new_assign(target, assign(), t);
    *rest = tok;
    return n;
}

static void init_bytes(Token **rest, Token *t, Type *ty,
                       unsigned char *buf, int bufsz, Global *g);
static int count_init_elements(Token **rest, Token *t, Type *elem);

/* 声明语句：返回初始化赋值链（可空）；static/extern 提升到全局 */
static Node *decl_stmt(void) {
    Token *t = tok;
    if (tok_is_kw(tok, "typedef"))
        error_at(tok, "typedef not supported inside function");
    Type *base = declspec();
    Node head = {0};
    Node *cur = &head;

    for (;;) {
        Token *name = NULL;
        Type *ty = declarator(base, &name);
        ty = func_to_ptr(ty);   /* int (*fp)(int) → 函数指针变量 */
        if (!name)
            error_at(tok, "declaration without name inside function");
        if (ty->kind == TY_VOID)
            error_at(t, "cannot declare a variable of type void");

        /* 局部不完整数组：int a[] = {1,2,3} → 推断长度 */
        bool inferred = false;
        if (ty->kind == TY_ARRAY && ty->array_len < 0) {
            if (!tok_is(tok, "="))
                error_at(t, "incomplete array without initializer");
            tok = tok->next;
            int len = count_init_elements(&tok, tok, ty->base);
            ty = ty_array(ty->base, len);
            inferred = true;
        }

        Node *target = NULL;
        Global *hoisted = NULL;

        if (decl_is_static) {
            /* static 局部 → 提升为全局（跨调用共享；同名合并） */
            hoisted = find_global(name);
            if (!hoisted) {
                hoisted = (Global *)calloc(1, sizeof(Global));
                if (!hoisted) { fprintf(stderr, "out of memory\n"); exit(1); }
                hoisted->name = xstrndup(name->loc, (size_t)name->len);
                hoisted->len = name->len;
                hoisted->ty = ty;
                hoisted->is_static = true;
                hoisted->next = globals;
                globals = hoisted;
            }
            Node *g = new_node(ND_GVAR, name);
            g->name = xstrndup(name->loc, (size_t)name->len);
            g->ty = ty;
            target = g;
        } else if (decl_is_extern) {
            /* extern 局部声明：引用全局（不存在则占位） */
            hoisted = find_global(name);
            if (!hoisted) {
                hoisted = (Global *)calloc(1, sizeof(Global));
                if (!hoisted) { fprintf(stderr, "out of memory\n"); exit(1); }
                hoisted->name = xstrndup(name->loc, (size_t)name->len);
                hoisted->len = name->len;
                hoisted->ty = ty;
                hoisted->is_extern = true;
                hoisted->next = globals;
                globals = hoisted;
            }
            Node *g = new_node(ND_GVAR, name);
            g->name = xstrndup(name->loc, (size_t)name->len);
            g->ty = ty;
            target = g;
        } else {
            /* 局部变量：栈槽按 align4(size) 分配 */
            locals_bytes += (ty->size + 3) & ~3;
            Var *v = (Var *)calloc(1, sizeof(Var));
            if (!v) { fprintf(stderr, "out of memory\n"); exit(1); }
            v->name = name->loc;
            v->len = name->len;
            v->ty = ty;
            v->offset = -locals_bytes;
            v->next = vars;
            vars = v;
            Node *vn = new_node(ND_VAR, name);
            vn->offset = v->offset;
            vn->ty = ty;
            target = vn;
        }

        if (inferred || tok_is(tok, "=")) {
            if (!inferred)
                tok = tok->next;
            if (hoisted && hoisted->is_extern)
                error_at(t, "extern variable cannot have initializer");
            if (hoisted) {
                /* static 局部初始化 → 全局初始化器（启动时一次，非每次调用） */
                hoisted->init_data = (unsigned char *)calloc(1, (size_t)ty->size);
                if (!hoisted->init_data) { fprintf(stderr, "out of memory\n"); exit(1); }
                hoisted->init_data_len = ty->size;
                init_bytes(&tok, tok, ty, hoisted->init_data, ty->size, hoisted);
            } else {
                Node *init = init_local(&tok, tok, ty, target);
                while (init) {
                    cur = cur->next = init;
                    init = init->next;
                }
            }
        } else if (hoisted && !hoisted->is_extern && !hoisted->init_data) {
            /* static 无初始化 → 全局清零（init_data 全 0） */
            hoisted->init_data = (unsigned char *)calloc(1, (size_t)ty->size);
            if (!hoisted->init_data) { fprintf(stderr, "out of memory\n"); exit(1); }
            hoisted->init_data_len = ty->size;
        }

        if (tok_is(tok, ",")) {
            tok = tok->next;
            continue;
        }
        break;
    }
    skip(";");
    return head.next;
}

/* stmt = 见文件头文法 */
static Node *stmt(void) {
    if (tok_is_kw(tok, "return")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binary(ND_RETURN, expr(), NULL, t);
        skip(";");
        return n;
    }
    if (is_declspec_start(tok)) {
        return decl_stmt();
    }
    if (tok_is_kw(tok, "if")) {
        Token *t = tok;
        tok = tok->next;
        skip("(");
        Node *cond = expr();
        skip(")");
        Node *then = stmt();
        Node *els = NULL;
        if (tok_is_kw(tok, "else")) {
            tok = tok->next;
            els = stmt();
        }
        Node *n = new_node(ND_IF, t);
        n->lhs = cond;
        n->rhs = then;
        n->els = els;
        return n;
    }
    if (tok_is_kw(tok, "while")) {
        Token *t = tok;
        tok = tok->next;
        skip("(");
        Node *cond = expr();
        skip(")");
        loop_depth++;
        breakable_depth++;
        Node *n = new_node(ND_WHILE, t);
        n->lhs = cond;
        n->rhs = stmt();
        breakable_depth--;
        loop_depth--;
        return n;
    }
    if (tok_is_kw(tok, "for")) {
        Token *t = tok;
        tok = tok->next;
        skip("(");
        Node *init = NULL, *cond = NULL, *inc = NULL;
        if (!tok_is(tok, ";"))
            init = expr();
        skip(";");
        if (!tok_is(tok, ";"))
            cond = expr();
        skip(";");
        if (!tok_is(tok, ")"))
            inc = expr();
        skip(")");
        loop_depth++;
        breakable_depth++;
        Node *n = new_node(ND_FOR, t);
        n->lhs = init;
        n->rhs = cond;
        n->els = inc;
        n->body = stmt();
        breakable_depth--;
        loop_depth--;
        return n;
    }
    if (tok_is_kw(tok, "do")) {
        Token *t = tok;
        tok = tok->next;
        loop_depth++;
        breakable_depth++;
        Node *n = new_node(ND_DOWHILE, t);
        n->lhs = stmt();
        breakable_depth--;
        loop_depth--;
        skip("while");
        skip("(");
        n->rhs = expr();
        skip(")");
        skip(";");
        return n;
    }
    if (tok_is_kw(tok, "switch")) {
        Token *t = tok;
        tok = tok->next;
        skip("(");
        Node *cond = expr();
        skip(")");
        breakable_depth++;
        /* case 收集器入栈（stmt 的 case/default 注册到栈顶） */
        if (case_depth >= 32)
            error_at(t, "switch nesting too deep");
        case_stack[case_depth++] = NULL;
        Node *body = stmt();
        Node *cases = case_stack[--case_depth];
        breakable_depth--;
        Node *n = new_node(ND_SWITCH, t);
        n->lhs = cond;
        n->rhs = cases;
        n->els = body;
        n->offset = -hidden_lvar(ty_int)->offset;   /* 条件值槽 */
        return n;
    }
    if (tok_is_kw(tok, "case") || tok_is_kw(tok, "default")) {
        /* case 常量: / default: —— 标签节点，注册到当前 switch 收集器；
         * 其后的语句在语句链中顺序生成（fallthrough 天然成立） */
        Token *t = tok;
        bool is_case = tok_is_kw(tok, "case");
        tok = tok->next;
        Node *n = new_node(is_case ? ND_CASE : ND_DEFAULT, t);
        if (is_case) {
            Node *v = assign();   /* 常量表达式（逗号非法） */
            if (v->kind != ND_NUM)
                error_at(v->tok, "case value is not a constant");
            n->val = v->val;
        }
        skip(":");
        n->num = lbl_num++;
        register_case(n, t);
        return n;
    }
    if (tok_is_kw(tok, "break")) {
        Token *t = tok;
        tok = tok->next;
        skip(";");
        if (!breakable_depth)
            error_at(t, "break outside loop or switch");
        return new_node(ND_BREAK, t);
    }
    if (tok_is_kw(tok, "continue")) {
        Token *t = tok;
        tok = tok->next;
        skip(";");
        if (!loop_depth)
            error_at(t, "continue outside loop");
        return new_node(ND_CONTINUE, t);
    }
    if (tok_is_kw(tok, "goto")) {
        Token *t = tok;
        tok = tok->next;
        if (tok->kind != TK_IDENT)
            error_at(tok, "expected label name");
        Node *n = new_node(ND_GOTO, t);
        n->num = label_of(tok);
        tok = tok->next;
        skip(";");
        return n;
    }
    if (tok->kind == TK_IDENT && tok->next && tok_is(tok->next, ":")) {
        /* label: 语句（label 定义） */
        Token *t = tok;
        Node *n = new_node(ND_LABEL, t);
        n->num = label_of(t);
        /* 置 defined：可能已被前向 goto 注册 */
        for (LblEnt *e = lbls; e; e = e->next)
            if (e->num == n->num)
                e->defined = true;
        tok = tok->next->next;
        n->rhs = stmt();
        return n;
    }
    if (tok_is(tok, "{")) {
        tok = tok->next;
        Node head = {0};
        Node *cur = &head;
        while (!tok_is(tok, "}")) {
            if (tok->kind == TK_EOF)
                error_at(tok, "unclosed '{'");
            Node *s = stmt();
            if (s) {
                cur->next = s;
                cur = s;
            }
        }
        skip("}");
        return head.next;
    }
    if (tok_is(tok, ";")) {
        tok = tok->next;
        return NULL;   /* 空语句 */
    }
    {
        Node *n = expr();
        skip(";");
        return n;
    }
}

/* ---------- 函数与全局 ---------- */

/* 注册函数定义体（覆盖 is_decl 原型；重复定义报错） */
static Func *funcdef(Type *fty, Token *t) {
    Func *f = find_func(t);
    if (!f) {
        f = (Func *)calloc(1, sizeof(Func));
        if (!f) { fprintf(stderr, "out of memory\n"); exit(1); }
        f->name = xstrndup(t->loc, (size_t)t->len);
        f->len = t->len;
        f->next = funcs;
        funcs = f;
    } else if (!f->is_decl) {
        error_at(t, "duplicate function definition");
    }
    f->is_decl = false;
    f->ret_ty = fty->base;
    f->fty = fty;
    f->nargs = fty->nargs;
    f->is_variadic = fty->is_variadic;
    f->is_knr = fty->is_knr;

    /* 本函数新的局部符号表、帧累计与标签表 */
    vars = NULL;
    locals_bytes = 0;
    lbls = NULL;
    lbl_num = 0;
    breakable_depth = loop_depth = 0;
    case_depth = 0;

    /* 参数 → 局部变量 */
    if (fty->is_knr) {
        /* K&R：后续声明序列（到 {）给出参数类型 */
        while (!tok_is(tok, "{")) {
            if (tok->kind == TK_EOF)
                error_at(tok, "expected '{' for function body");
            if (tok_is(tok, ";")) {
                tok = tok->next;   /* 容忍多余分号 */
                continue;
            }
            if (!is_declspec_start(tok))
                error_at(tok, "expected K&R parameter declaration");
            Type *base = declspec();
            Token *nm = NULL;
            Type *pt = declarator(base, &nm);
            skip(";");
            if (!nm)
                continue;
            /* 匹配参数名 → 填类型（nm->loc 非 NUL 结尾 → strncmp） */
            for (int i = 0; i < fty->nargs; i++) {
                if (fty->param_names[i] &&
                    nm->len == (int)strlen(fty->param_names[i]) &&
                    strncmp(fty->param_names[i], nm->loc, (size_t)nm->len) == 0) {
                    fty->param_tys[i] = pt;
                    break;
                }
            }
        }
        /* 未声明类型 → int（C89 隐式） */
        for (int i = 0; i < fty->nargs; i++)
            if (!fty->param_tys[i])
                fty->param_tys[i] = ty_int;
        /* 复制参数类型回 Func */
        for (int i = 0; i < fty->nargs; i++)
            f->param_tys[i] = fty->param_tys[i];
        for (int i = 0; i < fty->nargs; i++)
            f->param_names[i] = fty->param_names[i];
        /* 建参数 Var */
        for (int i = 0; i < fty->nargs; i++) {
            locals_bytes += 4;
            Var *v = (Var *)calloc(1, sizeof(Var));
            if (!v) { fprintf(stderr, "out of memory\n"); exit(1); }
            v->name = fty->param_names[i];
            v->len = (int)strlen(fty->param_names[i]);
            v->ty = fty->param_tys[i];
            v->offset = -locals_bytes;
            v->next = vars;
            vars = v;
        }
    } else {
        for (int i = 0; i < fty->nargs; i++) {
            f->param_tys[i] = fty->param_tys[i];
            f->param_names[i] = fty->param_names[i];
            if (!f->param_names[i])
                error_at(t, "parameter name missing in function definition");
            locals_bytes += 4;                 /* 参数 = 局部变量 */
            Var *v = (Var *)calloc(1, sizeof(Var));
            if (!v) { fprintf(stderr, "out of memory\n"); exit(1); }
            v->name = f->param_names[i];
            v->len = (int)strlen(f->param_names[i]);
            v->ty = f->param_tys[i];
            v->offset = -locals_bytes;
            v->next = vars;
            vars = v;
        }
    }

    /* 函数体 */
    if (!tok_is(tok, "{"))
        error_at(tok, "expected '{'");
    tok = tok->next;
    Node head = {0};
    Node *cur = &head;
    while (!tok_is(tok, "}")) {
        if (tok->kind == TK_EOF)
            error_at(tok, "unclosed '{'");
        Node *s = stmt();
        if (s) {
            /* stmt() 可能返回多节点链（声明初始化器）——推进到链尾 */
            while (s) {
                cur->next = s;
                cur = s;
                s = s->next;
            }
        }
    }
    skip("}");
    /* 未定义标签检查（goto 前向引用已注册但无定义） */
    for (LblEnt *e = lbls; e; e = e->next)
        if (!e->defined) {
            fprintf(stderr, "parse error at \"%.*s\": undefined label '%.*s'\n",
                    t->len, t->loc, e->len, e->name);
            exit(1);
        }
    f->body = head.next;
    f->frame_size = locals_bytes;
    return f;
}

/* 原型注册：已定义 → 忽略；已声明 → 覆盖；新 → 注册 */
static void register_proto(Type *fty, Token *t) {
    Func *f = find_func(t);
    if (f) {
        if (f->is_decl) {
            f->ret_ty = fty->base;
            f->fty = fty;
            f->nargs = fty->nargs;
            f->is_variadic = fty->is_variadic;
            f->is_knr = fty->is_knr;
            for (int i = 0; i < fty->nargs; i++) {
                f->param_tys[i] = fty->param_tys[i];
                f->param_names[i] = fty->param_names[i];
            }
        }
        /* 已定义：忽略（宽松） */
        return;
    }
    f = (Func *)calloc(1, sizeof(Func));
    if (!f) { fprintf(stderr, "out of memory\n"); exit(1); }
    f->name = xstrndup(t->loc, (size_t)t->len);
    f->len = t->len;
    f->ret_ty = fty->base;
    f->fty = fty;
    f->nargs = fty->nargs;
    f->is_variadic = fty->is_variadic;
    f->is_knr = fty->is_knr;
    f->is_decl = true;
    for (int i = 0; i < fty->nargs; i++) {
        f->param_tys[i] = fty->param_tys[i];
        f->param_names[i] = fty->param_names[i];
    }
    f->next = funcs;
    funcs = f;
}

/* 数组不完整声明：解析初始化器计数推断长度（只支持最外层）。
 * 不消费初始化器（*rest 指向其起始 token），由 init_bytes 重新解析。 */
static int count_init_elements(Token **rest, Token *t, Type *elem) {
    if (t->kind == TK_STR) {
        /* char 数组字符串初始化：str_len + NUL */
        if (elem->kind != TY_CHAR)
            error_at(t, "string initializer for non-char array");
        *rest = t;
        return t->str_len + 1;
    }
    if (tok_is(t, "{")) {
        int n = 0;
        Token *q = t->next;
        while (!tok_is(q, "}")) {
            if (q->kind == TK_EOF)
                error_at(q, "unclosed initializer");
            /* 平衡跳过单个元素（表达式可含嵌套 {} () 与逗号） */
            int depth = 0;
            for (;;) {
                if (q->kind == TK_EOF)
                    error_at(q, "unclosed initializer");
                if (tok_is(q, "{") || tok_is(q, "("))
                    depth++;
                if (tok_is(q, "}") || tok_is(q, ")")) {
                    if (depth == 0 && tok_is(q, "}"))
                        break;
                    depth--;
                }
                if (depth == 0 && tok_is(q, ","))
                    break;
                q = q->next;
            }
            n++;
            if (tok_is(q, ","))
                q = q->next;
        }
        *rest = t;   /* 指向 '{'：init_bytes 重新解析 */
        return n;
    }
    /* 标量：eval 跳过（不消费，init_bytes 重新解析） */
    Token *q = t;
    eval_const(&q, t);
    *rest = t;
    return 1;
}

/* 全局初始化器 → 字节流（大端；字符串引用记录到 str_relocs） */
static void init_bytes(Token **rest, Token *t, Type *ty,
                       unsigned char *buf, int bufsz, Global *g);

static void init_bytes(Token **rest, Token *t, Type *ty,
                       unsigned char *buf, int bufsz, Global *g) {
    if (ty->kind == TY_ARRAY) {
        int64_t len = ty->array_len;
        if (t->kind == TK_STR && ty->base->kind == TY_CHAR) {
            if (t->str_len > len)
                error_at(t, "string too long for array");
            memcpy(buf, t->str, (size_t)t->str_len);
            memset(buf + t->str_len, 0, (size_t)(len - t->str_len));
            *rest = t->next;
            return;
        }
        if (!tok_is(t, "{"))
            error_at(t, "array initializer must be '{' or string");
        tok = t->next;
        int64_t i;
        for (i = 0; i < len; i++) {
            if (tok_is(tok, "}"))
                break;
            if (i > 0)
                skip(",");
            init_bytes(&tok, tok, ty->base, buf + i * ty->base->size,
                       (int)ty->base->size, g);
        }
        if (!tok_is(tok, "}"))
            error_at(tok, "too many initializers");
        skip("}");
        *rest = tok;
        return;
    }
    if (ty->kind == TY_STRUCT) {
        if (!tok_is(t, "{"))
            error_at(t, "struct initializer must be '{'");
        tok = t->next;
        for (Member *m = ty->members; m; m = m->next) {
            if (tok_is(tok, "}"))
                break;
            if (m != ty->members)
                skip(",");
            if (m->bit_width >= 0) {
                /* 位域：读现有单元，or 入值 */
                int64_t v = eval_const(&tok, tok);
                unsigned char *u = buf + m->offset;
                uint32_t unit = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) |
                                ((uint32_t)u[2] << 8) | u[3];
                uint32_t mask = (m->bit_width >= 32) ? 0xFFFFFFFFu
                                                     : ((1u << m->bit_width) - 1);
                uint32_t shift = (uint32_t)(32 - m->bit_offset - m->bit_width);
                unit = (unit & ~(mask << shift)) | (((uint32_t)v & mask) << shift);
                u[0] = (unsigned char)(unit >> 24);
                u[1] = (unsigned char)(unit >> 16);
                u[2] = (unsigned char)(unit >> 8);
                u[3] = (unsigned char)unit;
            } else {
                init_bytes(&tok, tok, m->ty, buf + m->offset, m->ty->size, g);
            }
        }
        if (!tok_is(tok, "}"))
            error_at(tok, "too many initializers");
        skip("}");
        *rest = tok;
        return;
    }
    if (ty->kind == TY_UNION) {
        if (!tok_is(t, "{"))
            error_at(t, "union initializer must be '{'");
        tok = t->next;
        if (ty->members)
            init_bytes(&tok, tok, ty->members->ty, buf + ty->members->offset,
                       ty->members->ty->size, g);
        skip("}");
        *rest = tok;
        return;
    }
    /* 标量 */
    if (t->kind == TK_STR) {
        /* 字符串地址引用：交错记录（槽偏移 + 字符串编号），占位 0
         * （codegen 填 @s%d；编号 = 字符串在 str_head 链表中的位置） */
        if (g) {
            int sidx = 0;
            Token *s;
            for (s = str_head; s && s != t; s = s->next)
                sidx++;
            if (!s)
                error_at(t, "string literal not registered");
            g->str_relocs = (int *)realloc(g->str_relocs,
                                           (size_t)(2 * (g->n_str_relocs + 1)) * sizeof(int));
            if (!g->str_relocs) { fprintf(stderr, "out of memory\n"); exit(1); }
            g->str_relocs[2 * g->n_str_relocs] = (int)(buf - g->init_data);
            g->str_relocs[2 * g->n_str_relocs + 1] = sidx;
            g->n_str_relocs++;
        }
        memset(buf, 0, (size_t)bufsz);
        *rest = t->next;
        return;
    }
    /* 函数地址初始化器：int (*fp)(int) = add;（地址常量，运行时不可计算） */
    if (t->kind == TK_IDENT && find_func(t)) {
        if (g) {
            if (g->n_func_relocs >= 64)
                error_at(t, "too many function address initializers");
            g->func_reloc_offsets[g->n_func_relocs] = (int)(buf - g->init_data);
            g->func_reloc_names[g->n_func_relocs] = xstrndup(t->loc, (size_t)t->len);
            g->n_func_relocs++;
        }
        memset(buf, 0, (size_t)bufsz);
        *rest = t->next;
        return;
    }
    int64_t v = eval_const(&tok, tok);
    if (ty->kind == TY_CHAR) {
        buf[0] = (unsigned char)(v & 0xFF);
    } else {
        uint32_t w = (uint32_t)(v & 0xFFFFFFFFu);
        buf[0] = (unsigned char)(w >> 24);
        buf[1] = (unsigned char)(w >> 16);
        buf[2] = (unsigned char)(w >> 8);
        buf[3] = (unsigned char)w;
    }
    *rest = tok;
}

/* 预扫描：浅扫描预注册函数（int/char/unsigned/void 开头的顶层定义）。
 * 不解析类型（declspec/declarator 会误判函数体与函数指针参数），
 * 仅 ident 后跟 "(" 的模式匹配。struct/enum/typedef 开头的函数
 * 依赖隐式声明或定义顺序（parse 覆盖）。 */
static void pre_scan_functions(void) {
    for (Token *p = tok; p->kind != TK_EOF; p = p->next) {
        if (p->kind != TK_KEYWORD)
            continue;
        if (tok_is(p, "typedef") || tok_is(p, "struct") || tok_is(p, "union") ||
            tok_is(p, "enum")) {
            /* 跳过整条声明（嵌套 {} () 平衡） */
            int depth = 0;
            while (p->kind != TK_EOF) {
                if (tok_is(p, "{") || tok_is(p, "("))
                    depth++;
                else if (tok_is(p, "}") || tok_is(p, ")")) {
                    if (depth == 0 && tok_is(p, "}"))
                        break;   /* 函数体内的块结束？防御 */
                    depth--;
                } else if (tok_is(p, ";") && depth == 0)
                    break;
                p = p->next;
            }
            continue;
        }
        if (tok_is(p, "int") || tok_is(p, "char") || tok_is(p, "unsigned") ||
            tok_is(p, "void")) {
            /* 浅解析：存储类/类型关键字链 → * 链 → ident → "(" */
            Token *q = p;
            while (q->kind == TK_KEYWORD &&
                   (tok_is(q, "static") || tok_is(q, "extern") ||
                    tok_is(q, "const") || tok_is(q, "volatile") ||
                    tok_is(q, "register") || tok_is(q, "inline") ||
                    tok_is(q, "unsigned") || tok_is(q, "int") ||
                    tok_is(q, "char") || tok_is(q, "void")))
                q = q->next;
            while (tok_is(q, "*"))
                q = q->next;
            if (q->kind == TK_IDENT && q->next && tok_is(q->next, "(")) {
                /* 函数定义/原型：注册（is_decl=true 占位，parse 覆盖） */
                Func *f = find_func(q);
                if (!f) {
                    f = (Func *)calloc(1, sizeof(Func));
                    if (!f) { fprintf(stderr, "out of memory\n"); exit(1); }
                    f->name = xstrndup(q->loc, (size_t)q->len);
                    f->len = q->len;
                    f->ret_ty = ty_int;   /* 占位，parse 填 */
                    f->is_decl = true;
                    f->next = funcs;
                    funcs = f;
                }
                /* 跳过参数列表（到匹配 )）——防误注册 */
                int depth = 0;
                q = q->next;   /* 到 ( */
                while (q->kind != TK_EOF) {
                    if (tok_is(q, "(") || tok_is(q, "{"))
                        depth++;
                    else if (tok_is(q, ")") || tok_is(q, "}")) {
                        if (depth == 0)
                            break;
                        depth--;
                    }
                    q = q->next;
                }
            }
            /* p 继续前进会扫参数 token 内部——无副作用（仅 ident 模式匹配，
             * 参数名后跟 "(" 的可能：函数指针参数 (*cb)(int)——q 检查是
             * "*" 链后 ident + "("，参数里 (*cb)( 的 cb 前是 "(" 不是 "*"？
             * "(" 不是 "*"，q 停在 "(" → 不匹配 ✓ 安全）。 */
        }
    }
}

/* program = (typedef | struct/union/enum | funcdef | prototype | global)* */
Program *parse(Token *toks) {
    tok = toks;
    globals = NULL;
    funcs = NULL;
    tdefs = NULL;
    enums = NULL;
    tags = NULL;
    nstrings = 0;

    /* 类型单例初始化 */
    ty_int = (Type *)calloc(1, sizeof(Type));
    ty_char = (Type *)calloc(1, sizeof(Type));
    ty_void = (Type *)calloc(1, sizeof(Type));
    if (!ty_int || !ty_char || !ty_void) { fprintf(stderr, "out of memory\n"); exit(1); }
    ty_int->kind = TY_INT;
    ty_int->size = 4;
    ty_char->kind = TY_CHAR;
    ty_char->size = 1;
    ty_void->kind = TY_VOID;
    ty_void->size = 1;
    str_head = NULL;
    str_tail = &str_head;

    pre_scan_functions();

    Program *prog = (Program *)calloc(1, sizeof(Program));
    if (!prog) { fprintf(stderr, "out of memory\n"); exit(1); }

    while (tok->kind != TK_EOF) {
        if (!is_declspec_start(tok))
            error_at(tok, "expected type at top level");
        Token *t = tok;
        Type *base = declspec();

        /* 纯类型声明（struct P {..}; / enum {..};） */
        if (tok_is(tok, ";")) {
            tok = tok->next;
            continue;
        }

        if (decl_is_typedef) {
            /* typedef 声明（可多声明器） */
            for (;;) {
                Token *name = NULL;
                Type *ty = func_to_ptr(declarator(base, &name));
                if (!name)
                    error_at(t, "typedef requires a name");
                add_tdef(name, ty);
                if (tok_is(tok, ",")) {
                    tok = tok->next;
                    continue;
                }
                break;
            }
            skip(";");
            continue;
        }

        Token *name = NULL;
        Type *ty = declarator(base, &name);

        if (ty->kind == TY_FUNC) {
            /* 函数定义或原型 */
            if (tok_is(tok, "{")) {
                funcdef(ty, name);
            } else {
                if (!tok_is(tok, ";"))
                    error_at(tok, "expected ';' after function declaration");
                tok = tok->next;
                register_proto(ty, name);
            }
            continue;
        }

        if (!name)
            error_at(tok, "declaration without name");

        /* 全局变量声明 */
        ty = func_to_ptr(ty);   /* int (*fp)(int) → 函数指针变量 */
        if (ty->kind == TY_VOID)
            error_at(t, "'void' cannot declare a variable");

        /* 多声明器循环：int a, b[3], *c; */
        for (;;) {
            /* 数组不完整：int a[] = {...} 推断长度 */
            bool inferred = false;
            if (ty->kind == TY_ARRAY && ty->array_len < 0) {
                if (!tok_is(tok, "="))
                    error_at(t, "incomplete array without initializer");
                tok = tok->next;
                int len = count_init_elements(&tok, tok, ty->base);
                ty = ty_array(ty->base, len);
                inferred = true;   /* count_init_elements 已消费初始化器 */
            }
            if (ty->kind == TY_STRUCT && ty->size == 0 && !decl_is_extern)
                error_at(t, "incomplete struct variable");

            Global *g = NULL;
            Global *existing = find_global(name);
            if (existing) {
                if (decl_is_extern)
                    goto skip_global;   /* extern 声明在定义后：忽略 */
                if (existing->is_extern) {
                    existing->is_extern = false;
                    existing->ty = ty;
                    g = existing;       /* 定义补充 extern 占位 */
                } else {
                    error_at(name, "duplicate global variable");
                }
            }
            if (!g) {
                if (find_func(name))
                    error_at(name, "global name conflicts with function");
                g = (Global *)calloc(1, sizeof(Global));
                if (!g) { fprintf(stderr, "out of memory\n"); exit(1); }
                g->name = xstrndup(name->loc, (size_t)name->len);
                g->len = name->len;
                g->ty = ty;
                g->is_static = decl_is_static;
                g->is_extern = decl_is_extern;
                g->next = globals;
                globals = g;
            }
            if (inferred || tok_is(tok, "=")) {
                if (!inferred)
                    tok = tok->next;
                if (g->is_extern)
                    error_at(t, "extern variable cannot have initializer");
                g->init_data = (unsigned char *)calloc(1, (size_t)ty->size);
                if (!g->init_data) { fprintf(stderr, "out of memory\n"); exit(1); }
                g->init_data_len = ty->size;
                init_bytes(&tok, tok, ty, g->init_data, ty->size, g);
            } else if (!g->is_extern) {
                /* 未初始化全局：数据段清零（M1 行为；bss 迁移在 Task 5） */
                g->init_data = (unsigned char *)calloc(1, (size_t)ty->size);
                if (!g->init_data) { fprintf(stderr, "out of memory\n"); exit(1); }
                g->init_data_len = ty->size;
            }
        skip_global:
            if (tok_is(tok, ",")) {
                tok = tok->next;
                name = NULL;
                ty = declarator(base, &name);
                if (ty->kind == TY_FUNC)
                    error_at(t, "function declaration cannot be followed by ','");
                if (!name)
                    error_at(t, "declaration without name");
                continue;
            }
            skip(";");
            break;
        }
    }

    prog->globals = globals;
    prog->funcs = funcs;
    *str_tail = NULL;
    prog->strs = str_head;

    /* 必须有 main */
    for (Func *f = funcs; f; f = f->next)
        if (f->len == 4 && strncmp(f->name, "main", 4) == 0)
            return prog;
    fprintf(stderr, "parse error: no main function\n");
    exit(1);
}
