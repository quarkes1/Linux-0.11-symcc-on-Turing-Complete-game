/* symcc/src/parse.c — 递归下降语法分析
 *
 * M1 文法（int 全部有符号）：
 *   program   = (funcdef | global)*
 *   funcdef   = ("int" | "void") ident "(" params ")" "{" stmt* "}"
 *   params    = ε | "void" | ("int" ident ("," "int" ident)*)
 *   global    = "int" ident ("=" num)? ";"
 *   stmt      = "return" expr ";"
 *             | "{" stmt* "}"
 *             | "int" ident ("=" expr)? ";"
 *             | expr ";"
 *             | "if" "(" expr ")" stmt ("else" stmt)?
 *             | "while" "(" expr ")" stmt
 *             | "for" "(" expr? ";" expr? ";" expr? ")" stmt
 *   expr      = assign
 *   assign    = logor ("=" assign)?
 *   primary   = num | "(" expr ")" | ident | 字符字面量 | 字符串字面量
 *             | ident "(" args ")"          （函数调用）
 *
 * 局部变量：声明时分配栈槽（offset 从 4 起递增），记录在符号链表中。
 * 参数：也是局部变量；被调方入口 sp 指向返回地址，实参 k 在 [sp+4+4k]，
 * 序言负责拷入各自栈槽（见 codegen.c）。
 * 限制：被调函数须先定义（M1 单遍，test 均满足）；无作用域回收。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"

static Token *tok;

/* 局部变量符号表（链表，每个函数独立） */
typedef struct Var {
    struct Var *next;
    char *name;
    int len;
    int offset;      /* 相对 sp 的负偏移 */
} Var;

static Var *vars;
static int locals_bytes;   /* 当前函数帧大小累计 */

/* 全局变量表与函数表（定义顺序） */
static Global *globals;
static Func *funcs;

/* 字符串字面量编号与收集 */
static int nstrings;
static Token *str_head, **str_tail = &str_head;

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
    return n;
}

static Node *new_binary(int kind, Node *lhs, Node *rhs, Token *t) {
    Node *n = new_node(kind, t);
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}

static Node *expr(void);
static Node *stmt(void);

/* strndup 替代（mingw 可用但显式自给） */
static char *xstrndup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ---------- 表达式 ---------- */

/* primary = num | "(" expr ")" | ident | 字符/字符串字面量 | ident "(" args ")" */
static Node *primary(void) {
    if (tok->kind == TK_NUM) {
        Node *n = new_node(ND_NUM, tok);
        n->val = tok->val;
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
        *str_tail = tok;              /* 收集到程序级链表（数据段输出用） */
        str_tail = &tok->next;
        tok = tok->next;
        return n;
    }
    if (tok->kind == TK_IDENT) {
        /* 函数调用：ident "(" */
        if (tok->next && tok_is(tok->next, "(")) {
            Token *t = tok;
            for (Func *f = funcs; f; f = f->next)
                if (f->len == t->len && strncmp(f->name, t->loc, (size_t)f->len) == 0) {
                    tok = tok->next;          /* 跳到 ( */
                    Node *n = new_node(ND_CALL, t);
                    n->name = xstrndup(t->loc, (size_t)t->len);
                    n->val = 0;               /* 实参数 */
                    skip("(");
                    if (!tok_is(tok, ")")) {
                        Node head = {0}, *cur = &head;
                        cur = cur->next = expr();
                        n->val = 1;
                        while (tok_is(tok, ",")) {
                            tok = tok->next;
                            cur = cur->next = expr();
                            n->val++;
                        }
                        n->rhs = head.next;
                    }
                    skip(")");
                    return n;
                }
            error_at(t, "undefined function");
        }
        /* 局部变量 */
        for (Var *v = vars; v; v = v->next) {
            if (v->len == tok->len && strncmp(v->name, tok->loc, (size_t)v->len) == 0) {
                Node *n = new_node(ND_VAR, tok);
                n->offset = v->offset;
                tok = tok->next;
                return n;
            }
        }
        /* 全局变量 */
        for (Global *g = globals; g; g = g->next) {
            if (g->len == tok->len && strncmp(g->name, tok->loc, (size_t)g->len) == 0) {
                Node *n = new_node(ND_GVAR, tok);
                n->name = xstrndup(tok->loc, (size_t)tok->len);
                tok = tok->next;
                return n;
            }
        }
        error_at(tok, "undefined variable");
    }
    error_at(tok, "expected number, '(' or identifier");
    return NULL; /* 不可达 */
}

/* unary = ("-" | "!") unary | primary */
static Node *unary(void) {
    if (tok_is(tok, "-")) {
        Token *t = tok;
        tok = tok->next;
        return new_binary(ND_NEG, unary(), NULL, t);
    }
    if (tok_is(tok, "!")) {
        Token *t = tok;
        tok = tok->next;
        return new_binary(ND_NOT, unary(), NULL, t);
    }
    return primary();
}

/* mul = unary ("*" unary)* */
static Node *mul(void) {
    Node *n = unary();
    while (tok_is(tok, "*")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binary(ND_MUL, n, unary(), t);
    }
    return n;
}

/* additive = mul ("+" mul | "-" mul)* */
static Node *additive(void) {
    Node *n = mul();
    for (;;) {
        if (tok_is(tok, "+")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binary(ND_ADD, n, mul(), t);
        } else if (tok_is(tok, "-")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binary(ND_SUB, n, mul(), t);
        } else {
            return n;
        }
    }
}

/* relational = additive (("<" | "<=" | ">" | ">=") additive)* */
static Node *relational(void) {
    Node *n = additive();
    for (;;) {
        if (tok_is(tok, "<")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binary(ND_LT, n, additive(), t);
        } else if (tok_is(tok, "<=")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binary(ND_LE, n, additive(), t);
        } else if (tok_is(tok, ">")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binary(ND_GT, n, additive(), t);
        } else if (tok_is(tok, ">=")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binary(ND_GE, n, additive(), t);
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
            n = new_binary(ND_EQ, n, relational(), t);
        } else if (tok_is(tok, "!=")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binary(ND_NE, n, relational(), t);
        } else {
            return n;
        }
    }
}

/* logand = equality ("&&" equality)* */
static Node *logand(void) {
    Node *n = equality();
    while (tok_is(tok, "&&")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binary(ND_LOGAND, n, equality(), t);
    }
    return n;
}

/* logor = logand ("||" logand)* */
static Node *logor(void) {
    Node *n = logand();
    while (tok_is(tok, "||")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binary(ND_LOGOR, n, logand(), t);
    }
    return n;
}

/* assign = logor ("=" assign)? */
static Node *assign(void) {
    Node *n = logor();
    if (tok_is(tok, "=")) {
        Token *t = tok;
        tok = tok->next;
        if (n->kind != ND_VAR && n->kind != ND_GVAR)
            error_at(t, "assignment target is not a variable");
        n = new_binary(ND_ASSIGN, n, assign(), t);
    }
    return n;
}

/* expr = assign */
static Node *expr(void) {
    return assign();
}

/* ---------- 语句 ---------- */

/* 声明：int ident (= expr)? ; 返回 ND_VAR（无初始化）或 ND_ASSIGN */
static Node *decl_stmt(void) {
    Token *t = tok;
    tok = tok->next;   /* 吃掉 int */
    if (tok->kind != TK_IDENT)
        error_at(tok, "expected variable name");
    Token *name = tok;
    tok = tok->next;

    /* 分配栈槽（offset 为负：-4, -8, …） */
    locals_bytes += 4;
    Var *v = (Var *)calloc(1, sizeof(Var));
    if (!v) { fprintf(stderr, "out of memory\n"); exit(1); }
    v->name = name->loc;
    v->len = name->len;
    v->offset = -locals_bytes;
    v->next = vars;
    vars = v;

    Node *n = new_node(ND_VAR, name);
    n->offset = v->offset;

    if (tok_is(tok, "=")) {
        tok = tok->next;
        Node *init = new_binary(ND_ASSIGN, n, expr(), t);
        skip(";");
        return init;
    }
    skip(";");
    return n;   /* 声明无初始化：返回变量节点（codegen 忽略） */
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
    if (tok_is_kw(tok, "int")) {
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
        Node *n = new_node(ND_WHILE, t);
        n->lhs = cond;
        n->rhs = stmt();
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
        Node *n = new_node(ND_FOR, t);
        n->lhs = init;
        n->rhs = cond;
        n->els = inc;
        n->body = stmt();
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

/* funcdef = ("int" | "void") ident "(" params ")" "{" stmt* "}" */
static Func *funcdef(bool is_void, Token *t) {
    Func *f = (Func *)calloc(1, sizeof(Func));
    if (!f) { fprintf(stderr, "out of memory\n"); exit(1); }
    f->name = xstrndup(t->loc, (size_t)t->len);
    f->len = t->len;
    f->is_void = is_void;

    /* 先注册再解析函数体：允许自递归/互递归调用 */
    f->next = funcs;
    funcs = f;

    /* 本函数新的局部符号表与帧累计 */
    vars = NULL;
    locals_bytes = 0;

    skip("(");
    /* 参数：int ident (, int ident)*；空参或 void */
    if (tok_is_kw(tok, "void")) {
        tok = tok->next;
    } else {
        while (!tok_is(tok, ")")) {
            if (tok->kind == TK_EOF)
                error_at(tok, "unclosed '('");
            if (!tok_is_kw(tok, "int"))
                error_at(tok, "expected 'int' in parameter list");
            tok = tok->next;
            if (tok->kind != TK_IDENT)
                error_at(tok, "expected parameter name");
            Token *name = tok;
            tok = tok->next;

            locals_bytes += 4;                 /* 参数 = 局部变量 */
            Var *v = (Var *)calloc(1, sizeof(Var));
            if (!v) { fprintf(stderr, "out of memory\n"); exit(1); }
            v->name = name->loc;
            v->len = name->len;
            v->offset = -locals_bytes;
            v->next = vars;
            vars = v;
            f->nargs++;

            if (tok_is(tok, ","))
                tok = tok->next;
        }
    }
    skip(")");

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
            cur->next = s;
            cur = s;
        }
    }
    skip("}");
    f->body = head.next;
    f->frame_size = locals_bytes;
    return f;
}

/* program = (funcdef | global)* */
Program *parse(Token *toks) {
    tok = toks;
    globals = NULL;
    funcs = NULL;
    nstrings = 0;
    str_head = NULL;
    str_tail = &str_head;

    Program *prog = (Program *)calloc(1, sizeof(Program));
    if (!prog) { fprintf(stderr, "out of memory\n"); exit(1); }

    while (tok->kind != TK_EOF) {
        if (!tok_is_kw(tok, "int") && !tok_is_kw(tok, "void"))
            error_at(tok, "expected 'int' or 'void' at top level");
        bool is_void = tok_is_kw(tok, "void");
        tok = tok->next;

        if (tok->kind != TK_IDENT)
            error_at(tok, "expected name");
        Token *name = tok;
        tok = tok->next;

        if (tok_is(tok, "(")) {
            /* 函数定义（funcdef 内部自行注册到 funcs，允许自递归） */
            funcdef(is_void, name);
        } else {
            /* 全局变量声明：int ident (= num)? ; */
            if (is_void)
                error_at(tok, "'void' cannot declare a variable");
            Global *g = (Global *)calloc(1, sizeof(Global));
            if (!g) { fprintf(stderr, "out of memory\n"); exit(1); }
            g->name = xstrndup(name->loc, (size_t)name->len);
            g->len = name->len;
            g->init_val = 0;
            if (tok_is(tok, "=")) {
                tok = tok->next;
                if (tok->kind != TK_NUM)
                    error_at(tok, "global initializer must be a constant");
                g->init_val = tok->val;
                tok = tok->next;
            }
            skip(";");
            g->next = globals;
            globals = g;
        }
    }

    prog->globals = globals;
    prog->funcs = funcs;
    prog->strs = str_head;

    /* 必须有 main */
    for (Func *f = funcs; f; f = f->next)
        if (f->len == 4 && strncmp(f->name, "main", 4) == 0)
            return prog;
    fprintf(stderr, "parse error: no main function\n");
    exit(1);
}
