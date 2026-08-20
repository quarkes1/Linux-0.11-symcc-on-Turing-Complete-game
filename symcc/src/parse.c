/* symcc/src/parse.c — 递归下降语法分析
 *
 * M1 文法（单函数 main）：
 *   program   = funcdef
 *   funcdef   = "int" "main" "(" "void" ")" "{" stmt* "}"
 *              | "int" "main" "(" ")" "{" stmt* "}"
 *   stmt      = "return" expr ";" | "{" stmt* "}"
 *   expr      = mul ("+" mul | "-" mul)*
 *   mul       = unary ("*" unary)*
 *   unary     = ("-" unary) | primary
 *   primary   = num | "(" expr ")"
 *
 * AST 为单链（next 指向下一条语句）；语句与表达式均为 Node。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"

static Token *tok;

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

/* primary = num | "(" expr ")" */
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
    error_at(tok, "expected number or '('");
    return NULL; /* 不可达 */
}

/* unary = ("-" unary) | primary */
static Node *unary(void) {
    if (tok_is(tok, "-")) {
        Token *t = tok;
        tok = tok->next;
        return new_binary(ND_NEG, unary(), NULL, t);
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

/* expr = mul ("+" mul | "-" mul)* */
static Node *expr(void) {
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

/* stmt = "return" expr ";" | "{" stmt* "}" */
static Node *stmt(void) {
    if (tok_is_kw(tok, "return")) {
        Token *t = tok;
        tok = tok->next;
        Node *n = new_binary(ND_RETURN, expr(), NULL, t);
        skip(";");
        return n;
    }
    if (tok_is(tok, "{")) {
        tok = tok->next;
        Node head = {0};
        Node *cur = &head;
        while (!tok_is(tok, "}")) {
            if (tok->kind == TK_EOF)
                error_at(tok, "unclosed '{'");
            cur = cur->next = stmt();
        }
        skip("}");
        return head.next;
    }
    error_at(tok, "expected 'return' or '{'");
    return NULL; /* 不可达 */
}

/* program = funcdef */
Node *parse(Token *toks) {
    tok = toks;

    /* int main(void) / int main()（M1 词法器把 int 当标识符，按文本校验） */
    if (!(tok->kind == TK_IDENT && tok->len == 3 && strncmp(tok->loc, "int", 3) == 0))
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
            cur = cur->next = stmt();
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
