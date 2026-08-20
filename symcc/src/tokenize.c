/* symcc/src/tokenize.c — 词法分析
 *
 * M1 最小集：数字（十进制/0x）、关键字 return、标点 + - * ( ) { } = ; , 。
 * 错误直接 fprintf(stderr) + exit(1)（工具型编译器，M1 从简）。
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"

static Token *new_token(int kind, char *start, char *end) {
    Token *t = (Token *)calloc(1, sizeof(Token));
    if (!t) { fprintf(stderr, "out of memory\n"); exit(1); }
    t->kind = kind;
    t->loc = start;
    t->len = (int)(end - start);
    return t;
}

bool tok_is(const Token *t, const char *s) {
    return t->kind != TK_EOF
        && (t->kind == TK_PUNCT || t->kind == TK_KEYWORD)
        && t->len == (int)strlen(s)
        && strncmp(t->loc, s, (size_t)t->len) == 0;
}

bool tok_is_kw(const Token *t, const char *kw) {
    return t->kind == TK_KEYWORD
        && t->len == (int)strlen(kw)
        && strncmp(t->loc, kw, (size_t)t->len) == 0;
}

int64_t tok_num(const Token *t) {
    return t->kind == TK_NUM ? t->val : 0;
}

bool tok_at_bol(const Token *t) {
    return t && t->kind != TK_EOF && t->at_bol;
}

static bool is_punct1(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
           c == '(' || c == ')' ||
           c == '{' || c == '}' || c == '=' || c == ';' || c == ',' ||
           c == '[' || c == ']' ||
           c == '<' || c == '>' || c == '!' || c == '&' ||
           c == '#' || c == '?' || c == ':' || c == '^' || c == '~' ||
           c == '|' || c == '.';
}

/* 三字符运算符（<<= >>= ...；按最长匹配优先） */
static bool is_punct3(const char *p) {
    static const char *ops[] = { "<<=", ">>=", "..." };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++)
        if (p[0] == ops[i][0] && p[1] == ops[i][1] && p[2] == ops[i][2])
            return true;
    return false;
}

/* 双字符运算符（按最长匹配优先） */
static bool is_punct2(const char *p) {
    static const char *ops[] = { "==", "!=", "<=", ">=", "&&", "||", "##",
                                 "<<", ">>", "+=", "-=", "*=", "/=", "%=",
                                 "&=", "|=", "^=", "++", "--", "->" };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++)
        if (p[0] == ops[i][0] && p[1] == ops[i][1])
            return true;
    return false;
}

/* 关键字表（识别靠词边界） */
static const char *keywords[] = {
    "return", "int", "char", "unsigned", "void", "if", "else", "while", "for",
    "typedef", "enum", "struct", "union", "static", "extern", "const",
    "volatile", "register", "inline", "sizeof", "break", "continue",
    "do", "switch", "case", "default", "goto",
};

/* 解析转义序列（'\\' 已消费）；返回字节值，*pp 指向转义后的下一字符。
 * 支持 \n \t \r \v \f \a \b \\ \' \" \? \xHH \OOO；未知转义保留
 * 字面量（返回 '\\' 且只消费一个字符——Windows 路径等安全通过）。 */
static int get_escape(const char **pp) {
    const char *p = *pp;
    switch (*p) {
    case 'n': (*pp)++; return '\n';
    case 't': (*pp)++; return '\t';
    case 'r': (*pp)++; return '\r';
    case 'v': (*pp)++; return '\v';
    case 'f': (*pp)++; return '\f';
    case 'a': (*pp)++; return '\a';
    case 'b': (*pp)++; return '\b';
    case '\\': (*pp)++; return '\\';
    case '"': (*pp)++; return '"';
    case '\'': (*pp)++; return '\'';
    case '?': (*pp)++; return '?';
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7': {
        int val = 0, n = 0;
        while (n < 3 && *p >= '0' && *p <= '7') { val = val * 8 + (*p - '0'); p++; n++; }
        *pp = p;
        return val & 0xFF;
    }
    case 'x': {
        p++;
        if (!isxdigit((unsigned char)*p)) { (*pp) = p; return 'x'; }
        int val = 0;
        while (isxdigit((unsigned char)*p)) {
            int d;
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else d = *p - 'A' + 10;
            val = val * 16 + d;
            p++;
        }
        *pp = p;
        return val & 0xFF;
    }
    default:
        (*pp)++;
        return '\\';   /* 未知转义：保留反斜杠（后续字符按普通字符读入） */
    }
}

Token *tokenize(const char *p) {
    Token head = {0};
    Token *cur = &head;
    bool bol = true;   /* 下一个 token 位于其源码行首（'\n' 后置真，产 token 后置假） */

    while (*p) {
        if (isspace((unsigned char)*p)) {
            if (*p == '\n')
                bol = true;
            p++;
            continue;
        }

        /* 注释：斜杠斜杠 行注释 与 斜杠星 块注释 */
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            const char *start = p;
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (!*p) {
                fprintf(stderr, "unterminated block comment: %s\n", start);
                exit(1);
            }
            p += 2;
            continue;
        }

        /* 数字：十进制或 0x 十六进制 */
        if (isdigit((unsigned char)*p)) {
            char *start = (char *)p;
            int64_t val = 0;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                if (!isxdigit((unsigned char)*p)) {
                    fprintf(stderr, "bad hex literal: %s\n", start);
                    exit(1);
                }
                while (isxdigit((unsigned char)*p)) {
                    int d;
                    if (*p >= '0' && *p <= '9') d = *p - '0';
                    else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
                    else d = *p - 'A' + 10;
                    val = val * 16 + d;
                    p++;
                }
            } else {
                while (isdigit((unsigned char)*p)) {
                    val = val * 10 + (*p - '0');
                    p++;
                }
            }
            /* u/U 后缀 → unsigned 字面量 */
            bool unsuf = (*p == 'u' || *p == 'U');
            if (unsuf)
                p++;
            if (!isalnum((unsigned char)*p) && *p != '_') {
                Token *t = new_token(TK_NUM, start, (char *)p);
                t->val = val;
                t->is_unsigned = unsuf;
                t->at_bol = bol;
                bol = false;
                cur = cur->next = t;
                continue;
            }
            /* 数字后紧跟字母：非法（如 123abc、123ub） */
            fprintf(stderr, "invalid number: %s\n", start);
            exit(1);
        }

        /* 关键字 / 标识符 */
        if (isalpha((unsigned char)*p) || *p == '_') {
            char *start = (char *)p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
                size_t klen = strlen(keywords[i]);
                if ((size_t)(p - start) == klen && strncmp(start, keywords[i], klen) == 0) {
                    cur = cur->next = new_token(TK_KEYWORD, start, (char *)p);
                    cur->at_bol = bol;
                    bol = false;
                    goto next_char;
                }
            }
            cur = cur->next = new_token(TK_IDENT, start, (char *)p);
            cur->at_bol = bol;
            bol = false;
            continue;
        }

        /* 标点：三字符（<<= >>= ...）→ 双字符 → 单字符 */
        if (is_punct3(p)) {
            cur = cur->next = new_token(TK_PUNCT, (char *)p, (char *)p + 3);
            cur->at_bol = bol;
            bol = false;
            p += 3;
            continue;
        }
        if (is_punct2(p)) {
            cur = cur->next = new_token(TK_PUNCT, (char *)p, (char *)p + 2);
            cur->at_bol = bol;
            bol = false;
            p += 2;
            continue;
        }
        if (is_punct1(*p)) {
            cur = cur->next = new_token(TK_PUNCT, (char *)p, (char *)p + 1);
            cur->at_bol = bol;
            bol = false;
            p++;
            continue;
        }

        /* 字符串字面量 "..."（完整 C89 转义表，展开后存入 t->str） */
        if (*p == '"') {
            char *start = (char *)p;
            size_t cap = strcspn(p + 1, "\"") + 1;   /* 上限：源文本长度 */
            char *buf = (char *)malloc(cap);
            int blen = 0;
            p++;
            while (*p && *p != '"') {
                if (*p == '\\') {
                    p++;
                    buf[blen++] = get_escape(&p);
                } else {
                    buf[blen++] = *p++;
                }
            }
            if (*p != '"') {
                fprintf(stderr, "unterminated string literal: %s\n", start);
                exit(1);
            }
            p++;
            Token *t = new_token(TK_STR, start, (char *)p);
            t->str = buf;
            t->str_len = blen;
            t->at_bol = bol;
            bol = false;
            cur = cur->next = t;
            continue;
        }

        /* 单字符字面量 'A'（完整 C89 转义表） */
        if (*p == '\'') {
            char *start = (char *)p;
            p++;
            int64_t val;
            if (*p == '\\') {
                p++;
                val = get_escape(&p);
            } else {
                val = (unsigned char)*p;
                p++;
            }
            if (*p != '\'') {
                fprintf(stderr, "unterminated character literal: %s\n", start);
                exit(1);
            }
            p++;
            Token *t = new_token(TK_NUM, start, (char *)p);
            t->val = val;
            t->at_bol = bol;
            bol = false;
            cur = cur->next = t;
            continue;
        }

        fprintf(stderr, "unexpected character: '%c' (0x%02x)\n", *p, (unsigned char)*p);
        exit(1);

    next_char:
        ;
    }

    cur = cur->next = new_token(TK_EOF, (char *)p, (char *)p);
    return head.next;
}
