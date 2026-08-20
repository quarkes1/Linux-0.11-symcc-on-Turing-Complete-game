/* symcc/src/parse.c — 递归下降语法分析
 *
 * M1 文法（char 有符号，unsigned 仅标注供 Task 8 消费）：
 *   program   = (funcdef | global)*
 *   funcdef   = declspec ident "(" params ")" "{" stmt* "}"
 *   params    = ε | "void" | (declspec declarator ("," declspec declarator)*)
 *   global    = declspec declarator ("=" num)? ";"
 *   stmt      = "return" expr ";"
 *             | "{" stmt* "}"
 *             | declspec declarator ("=" expr)? ";"
 *             | expr ";"
 *             | "if" "(" expr ")" stmt ("else" stmt)?
 *             | "while" "(" expr ")" stmt
 *             | "for" "(" expr? ";" expr? ";" expr? ")" stmt
 *   expr      = assign
 *   assign    = logor ("=" assign)?
 *   unary     = ("-" | "!" | "&" | "*") unary | primary
 *   primary   = num | "(" expr ")" | ident | 字符/字符串字面量
 *             | ident "(" args ")"          （函数调用）
 *
 * 局部变量：声明时分配栈槽（offset 从 4 起递增，全部 4 字节），记录在
 * 符号链表中；char 变量也占 4 字节槽，读写用 load_8/store_8 + 符号扩展。
 * 参数：也是局部变量；被调方入口 sp 指向返回地址，实参 k 在 [sp+4+4k]，
 * 序言负责拷入各自栈槽（见 codegen.c）。
 * 限制：被调函数须先定义（M1 单遍，test 均满足）；无作用域回收；
 * & 只允许取变量/字符串地址（M1 无复合左值）。
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
    t->base = base;
    return t;
}

/* 无符号 int 类型（单例副本：不污染 ty_int） */
static Type *ty_uint(void) {
    static Type t;
    t = *ty_int;
    t.is_unsigned = true;
    return &t;
}

/* 局部变量符号表（链表，每个函数独立） */
typedef struct Var {
    struct Var *next;
    char *name;
    int len;
    int offset;      /* 相对 sp 的负偏移 */
    Type *ty;
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

/* 二元运算：默认 int 类型；指针算术 p+n/p-n 结果为指针类型
 * （p-q 两个指针相减 → int，即元素个数差）；
 * 算术运算（含 / %）任一侧 unsigned → 结果为 unsigned（C 语义） */
static Node *new_binop(int kind, Node *lhs, Node *rhs, Token *t) {
    Node *n = new_binary(kind, lhs, rhs, t);
    n->ty = ty_int;
    if ((kind == ND_ADD || kind == ND_SUB) &&
        lhs->ty->kind == TY_PTR && rhs->ty->kind != TY_PTR) {
        n->ty = lhs->ty;
    } else if ((kind == ND_ADD || kind == ND_SUB || kind == ND_MUL ||
                kind == ND_DIV || kind == ND_MOD) &&
               (lhs->ty->is_unsigned || rhs->ty->is_unsigned)) {
        n->ty = ty_uint();
    }
    return n;
}

/* declspec = ("unsigned"? ("int" | "char")) | "void" */
static Type *declspec(void) {
    bool is_unsigned = false;
    if (tok_is_kw(tok, "unsigned")) {
        is_unsigned = true;
        tok = tok->next;
    }
    if (tok_is_kw(tok, "void")) {
        if (is_unsigned)
            error_at(tok, "'void' cannot be unsigned");
        tok = tok->next;
        return ty_void;
    }
    if (tok_is_kw(tok, "int") || (is_unsigned && tok->kind == TK_IDENT)) {
        /* unsigned 后无 int/char → 缺省 unsigned int */
        if (tok_is_kw(tok, "int"))
            tok = tok->next;
        if (is_unsigned) {
            Type *t = (Type *)calloc(1, sizeof(Type));
            if (!t) { fprintf(stderr, "out of memory\n"); exit(1); }
            *t = *ty_int;
            t->is_unsigned = true;
            return t;              /* 独立副本：不污染 int 单例 */
        }
        return ty_int;
    }
    if (tok_is_kw(tok, "char")) {
        tok = tok->next;
        if (is_unsigned) {
            Type *t = (Type *)calloc(1, sizeof(Type));
            if (!t) { fprintf(stderr, "out of memory\n"); exit(1); }
            *t = *ty_char;
            t->is_unsigned = true;
            return t;
        }
        return ty_char;
    }
    error_at(tok, "expected type specifier");
    return NULL; /* 不可达 */
}

/* declarator = "*"* ident；返回完整类型，名字写入 *name */
static Type *declarator(Type *base, Token **name) {
    while (tok_is(tok, "*")) {
        base = ty_ptr(base);
        tok = tok->next;
    }
    if (tok->kind != TK_IDENT)
        error_at(tok, "expected name");
    *name = tok;
    tok = tok->next;
    return base;
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
                    n->ty = f->ret_ty;
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
        error_at(tok, "undefined variable");
    }
    error_at(tok, "expected number, '(' or identifier");
    return NULL; /* 不可达 */
}

/* unary = ("-" | "!" | "&" | "*") unary | primary */
static Node *unary(void) {
    if (tok_is(tok, "-")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binop(ND_NEG, unary(), NULL, t);
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
    if (tok_is(tok, "&")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binop(ND_ADDR, unary(), NULL, t);
        if (n->lhs->kind != ND_VAR && n->lhs->kind != ND_GVAR && n->lhs->kind != ND_STR)
            error_at(t, "invalid operand for '&'");
        n->ty = ty_ptr(n->lhs->ty);
        return n;
    }
    if (tok_is(tok, "*")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binary(ND_DEREF, unary(), NULL, t);
        if (n->lhs->ty->kind != TY_PTR)
            error_at(t, "dereference of non-pointer");
        n->ty = n->lhs->ty->base;
        return n;
    }
    return primary();
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

/* relational = additive (("<" | "<=" | ">" | ">=") additive)* */
static Node *relational(void) {
    Node *n = additive();
    for (;;) {
        if (tok_is(tok, "<")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_LT, n, additive(), t);
        } else if (tok_is(tok, "<=")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_LE, n, additive(), t);
        } else if (tok_is(tok, ">")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_GT, n, additive(), t);
        } else if (tok_is(tok, ">=")) {
            Token *t = tok;
            tok = tok->next;
            n = new_binop(ND_GE, n, additive(), t);
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

/* logand = equality ("&&" equality)* */
static Node *logand(void) {
    Node *n = equality();
    while (tok_is(tok, "&&")) {
        Token *t = tok;
        tok = tok->next;
        n = new_binop(ND_LOGAND, n, equality(), t);
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

/* assign = logor ("=" assign)? */
static Node *assign(void) {
    Node *n = logor();
    if (tok_is(tok, "=")) {
        Token *t = tok;
        tok = tok->next;
        if (n->kind != ND_VAR && n->kind != ND_GVAR && n->kind != ND_DEREF)
            error_at(t, "assignment target is not a variable");
        Node *an = new_binary(ND_ASSIGN, n, assign(), t);
        an->ty = n->ty;            /* 赋值表达式类型 = 目标类型 */
        n = an;
    }
    return n;
}

/* expr = assign */
static Node *expr(void) {
    return assign();
}

/* ---------- 语句 ---------- */

/* 声明：declspec declarator (= expr)? ; 返回 ND_VAR（无初始化）或 ND_ASSIGN */
static Node *decl_stmt(void) {
    Token *t = tok;
    Type *base = declspec();
    Token *name;
    Type *ty = declarator(base, &name);
    if (ty->kind == TY_VOID)
        error_at(t, "cannot declare a variable of type void");

    /* 分配栈槽（offset 为负：-4, -8, …；char 也占 4 字节） */
    locals_bytes += 4;
    Var *v = (Var *)calloc(1, sizeof(Var));
    if (!v) { fprintf(stderr, "out of memory\n"); exit(1); }
    v->name = name->loc;
    v->len = name->len;
    v->ty = ty;
    v->offset = -locals_bytes;
    v->next = vars;
    vars = v;

    Node *n = new_node(ND_VAR, name);
    n->offset = v->offset;
    n->ty = ty;

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
    if (tok_is_kw(tok, "int") || tok_is_kw(tok, "char") || tok_is_kw(tok, "unsigned")) {
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

/* 查找已注册函数（pre_scan 注册） */
static Func *find_func(Token *t) {
    for (Func *f = funcs; f; f = f->next)
        if (f->len == t->len && strncmp(f->name, t->loc, (size_t)t->len) == 0)
            return f;
    return NULL;
}

/* funcdef = ("int" | "void") ident "(" params ")" "{" stmt* "}"
 * 函数已在 pre_scan 注册（跨文件/前向引用/重复检测），这里填充其余字段。 */
static Func *funcdef(Type *ret_ty, Token *t) {
    Func *f = find_func(t);
    if (!f) {
        fprintf(stderr, "internal error: funcdef for unregistered function\n");
        exit(1);
    }
    f->ret_ty = ret_ty;

    /* 本函数新的局部符号表与帧累计 */
    vars = NULL;
    locals_bytes = 0;

    skip("(");
    /* 参数：declspec declarator (, declspec declarator)*；空参或 void */
    if (tok_is_kw(tok, "void")) {
        tok = tok->next;
    } else {
        while (!tok_is(tok, ")")) {
            if (tok->kind == TK_EOF)
                error_at(tok, "unclosed '('");
            if (!tok_is_kw(tok, "int") && !tok_is_kw(tok, "char") && !tok_is_kw(tok, "unsigned"))
                error_at(tok, "expected type in parameter list");
            Token *name;
            Type *pt = declarator(declspec(), &name);

            locals_bytes += 4;                 /* 参数 = 局部变量 */
            Var *v = (Var *)calloc(1, sizeof(Var));
            if (!v) { fprintf(stderr, "out of memory\n"); exit(1); }
            v->name = name->loc;
            v->len = name->len;
            v->ty = pt;
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

/* 预扫描：先注册全部函数定义（名字 + 返回类型）。
 * 用途：① 跨文件多文件编译（文件顺序无关）；② 允许前向/互递归调用；
 * ③ 重复定义检测（同一函数在第二个文件再定义 → 报错）。
 * 规则：类型关键字后跟 declarator 再接 "(" 即为函数定义。类型关键字
 * 不可能用作标识符，因此扫描时命中类型关键字即可安全尝试匹配。 */
static void pre_scan_functions(void) {
    Token *saved = tok;
    for (Token *p = tok; p->kind != TK_EOF; p = p->next) {
        if (p->kind != TK_KEYWORD ||
            !(tok_is(p, "int") || tok_is(p, "char") ||
              tok_is(p, "unsigned") || tok_is(p, "void")))
            continue;
        tok = p;
        Type *base = declspec();
        if (tok_is(tok, ")") && base == ty_void) {
            /* 裸 void 参数（void f(void) 中括号内的 void） */
            continue;
        }
        Token *name;
        Type *ty = declarator(base, &name);
        if (!tok_is(tok, "("))
            continue;                          /* 变量声明，跳过 */
        if (find_func(name))
            error_at(name, "duplicate function definition");
        Func *f = (Func *)calloc(1, sizeof(Func));
        if (!f) { fprintf(stderr, "out of memory\n"); exit(1); }
        f->name = xstrndup(name->loc, (size_t)name->len);
        f->len = name->len;
        f->ret_ty = ty;
        f->next = funcs;
        funcs = f;
    }
    tok = saved;
}

/* program = (funcdef | global)* */
Program *parse(Token *toks) {
    tok = toks;
    globals = NULL;
    funcs = NULL;
    nstrings = 0;

    /* 类型单例初始化 */
    ty_int = (Type *)calloc(1, sizeof(Type));
    ty_char = (Type *)calloc(1, sizeof(Type));
    ty_void = (Type *)calloc(1, sizeof(Type));
    if (!ty_int || !ty_char || !ty_void) { fprintf(stderr, "out of memory\n"); exit(1); }
    ty_int->kind = TY_INT;
    ty_char->kind = TY_CHAR;
    ty_void->kind = TY_VOID;
    str_head = NULL;
    str_tail = &str_head;

    pre_scan_functions();

    Program *prog = (Program *)calloc(1, sizeof(Program));
    if (!prog) { fprintf(stderr, "out of memory\n"); exit(1); }

    while (tok->kind != TK_EOF) {
        if (!tok_is_kw(tok, "int") && !tok_is_kw(tok, "char") &&
            !tok_is_kw(tok, "unsigned") && !tok_is_kw(tok, "void"))
            error_at(tok, "expected type at top level");
        Type *base = declspec();
        Token *name;
        Type *ty = declarator(base, &name);

        if (tok_is(tok, "(")) {
            /* 函数定义（funcdef 内部自行注册到 funcs，允许自递归） */
            funcdef(ty, name);
        } else {
            /* 全局变量声明：declspec declarator (= num)? ; */
            if (ty->kind == TY_VOID)
                error_at(tok, "'void' cannot declare a variable");
            /* 跨文件合并：全局重名、或与函数重名 → 报错 */
            for (Global *x = globals; x; x = x->next)
                if (x->len == name->len && strncmp(x->name, name->loc, (size_t)name->len) == 0)
                    error_at(name, "duplicate global variable");
            if (find_func(name))
                error_at(name, "global name conflicts with function");
            Global *g = (Global *)calloc(1, sizeof(Global));
            if (!g) { fprintf(stderr, "out of memory\n"); exit(1); }
            g->name = xstrndup(name->loc, (size_t)name->len);
            g->len = name->len;
            g->ty = ty;
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
