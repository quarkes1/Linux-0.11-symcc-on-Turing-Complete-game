/* symcc/src/parse.c — 递归下降语法分析
 *
 * M1 文法（单函数 main，int 全部有符号）：
 *   program   = funcdef
 *   funcdef   = "int" "main" "(" "void"? ")" "{" stmt* "}"
 *   stmt      = "return" expr ";"
 *             | "{" stmt* "}"
 *             | "int" ident ("=" expr)? ";"
 *             | expr ";"
 *             | "if" "(" expr ")" stmt ("else" stmt)?
 *             | "while" "(" expr ")" stmt
 *             | "for" "(" expr? ";" expr? ";" expr? ")" stmt
 *   expr      = assign
 *   assign    = equality ("=" assign)?         （右结合）
 *   equality  = relational (("==" | "!=") relational)*
 *   relational= additive (("<" | "<=" | ">" | ">=") additive)*
 *   additive  = mul ("+" mul | "-" mul)*
 *   mul       = unary ("*" unary)*
 *   unary     = ("-" | "!") unary | primary
 *   primary   = num | "(" expr ")" | ident | 字符字面量
 *
 * 局部变量：声明时分配栈槽（offset 从 4 起递增），记录在符号链表中。
 * 符号查找为线性扫描（M1 变量少，从简；无作用域回收，块内声明也进同一帧）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"

static Token *tok;

/* 局部变量符号表（链表） */
typedef struct Var {
    struct Var *next;
    char *name;
    int len;
    int offset;      /* 相对 sp 的负偏移 */
} Var;

static Var *vars;

static int locals_bytes;   /* 帧大小累计 */

int symcc_frame_size(void) {
    return locals_bytes;
}

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

/* ---------- 表达式 ---------- */

/* primary = num | "(" expr ")" | ident | 字符字面量（TK_NUM） */
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
    if (tok->kind == TK_IDENT) {
        for (Var *v = vars; v; v = v->next) {
            if (v->len == tok->len && strncmp(v->name, tok->loc, (size_t)v->len) == 0) {
                Node *n = new_node(ND_VAR, tok);
                n->offset = v->offset;
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

/* assign = equality ("=" assign)? */
static Node *assign(void) {
    Node *n = logor();
    if (tok_is(tok, "=")) {
        Token *t = tok;
        tok = tok->next;
        if (n->kind != ND_VAR)
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

/* 声明：int ident (= expr)? ;  */
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

/* program = funcdef */
Node *parse(Token *toks) {
    tok = toks;

    /* int main(void) / int main() */
    if (!tok_is_kw(tok, "int"))
        error_at(tok, "expected 'int'");
    tok = tok->next;

    if (!(tok->kind == TK_IDENT && tok->len == 4 && strncmp(tok->loc, "main", 4) == 0))
        error_at(tok, "expected 'main'");
    tok = tok->next;
    skip("(");
    if (tok_is(tok, "void"))           /* 空参或显式 void */
        tok = tok->next;
    skip(")");

    Node *body = NULL;
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
        body = head.next;
    } else {
        error_at(tok, "expected '{'");
    }

    if (tok->kind != TK_EOF)
        error_at(tok, "unexpected token after function");

    return body;
}
