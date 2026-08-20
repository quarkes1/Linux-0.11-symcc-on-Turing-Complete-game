/* symcc/src/preprocess.c — M2 Task 1 预处理器（token 流级）
 *
 * 支持：
 *   #include   — "..." 相对当前文件目录 → -I 目录 → 当前工作目录；
 *                <...> 仅 -I 目录（64 层嵌套上限防循环）
 *   #define    — 对象宏/函数宏；# 字符串化；## 粘贴；递归展开防自展开；
 *                同名重定义直接覆盖
 *   #undef
 *   #if/#ifdef/#ifndef/#elif/#else/#endif — 常量表达式求值（未定义标识符=0，
 *                defined 两种形态，除零按 0 处理）
 *   -D 命令行宏（"NAME" 或 "NAME=VALUE"）
 *   续行（反斜杠-换行，tokenize 之前合并）与注释剥离（同样在 tokenize 前）
 * 错误一律 fprintf(stderr) + exit(1)（工具型编译器，M1 约定）。
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"

/* ---------- 小工具 ---------- */

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static char *xstrndup(const char *s, int n) {
    char *p = (char *)xmalloc((size_t)n + 1);
    memcpy(p, s, (size_t)n);
    p[n] = 0;
    return p;
}

/* 读文件全文（NUL 结尾）。失败 exit(1)。从 main.c 复制，保持模块独立。 */
static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long len;
    char *buf;
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (char *)xmalloc((size_t)len + 1);
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        fprintf(stderr, "read error: %s\n", path);
        exit(1);
    }
    buf[len] = 0;
    fclose(fp);
    return buf;
}

/* ---------- 阶段 1+2：续行合并 + 注释剥离（tokenize 之前对源文本执行） ---------- */

/* 把 src 处理为：反斜杠-换行拼接删除；行注释（斜杠斜杠）与块注释
 * （斜杠星...星斜杠）替换为空白（块注释内的换行保留，at_bol/行号判定
 * 不受影响）。字符串/字符字面量内的内容不动（续行除外——C 标准阶段 2
 * 在字符串内同样拼接）。 */
static char *phase12(const char *src) {
    size_t len = strlen(src);
    char *out = (char *)xmalloc(len + 1);
    size_t o = 0;
    const char *p = src;
    int state = 0;   /* 0=普通 1=字符串 2=字符字面量 */
    while (*p) {
        if (p[0] == '\\' && p[1] == '\n') {
            p += 2;
            continue;
        }
        if (p[0] == '\\' && p[1] == '\r' && p[2] == '\n') {
            p += 3;
            continue;
        }
        if (state == 0) {
            if (p[0] == '/' && p[1] == '/') {
                while (*p && *p != '\n') p++;
                continue;
            }
            if (p[0] == '/' && p[1] == '*') {
                p += 2;
                while (*p && !(p[0] == '*' && p[1] == '/')) {
                    out[o++] = (*p == '\n') ? '\n' : ' ';
                    p++;
                }
                if (!*p) {
                    fprintf(stderr, "unterminated block comment\n");
                    exit(1);
                }
                p += 2;
                continue;
            }
            if (*p == '"') {
                out[o++] = *p++;
                state = 1;
                continue;
            }
            if (*p == '\'') {
                out[o++] = *p++;
                state = 2;
                continue;
            }
            out[o++] = *p++;
        } else {
            out[o++] = *p;
            if (*p == '\\' && p[1]) {          /* 转义：跳过下一个字符 */
                out[o++] = p[1];
                p += 2;
                continue;
            }
            if ((state == 1 && *p == '"') || (state == 2 && *p == '\''))
                state = 0;
            p++;
        }
    }
    out[o] = 0;
    return out;
}

/* ---------- 宏表 ---------- */

typedef struct Macro {
    struct Macro *next;
    char *name;          /* NUL 结尾 */
    int len;
    char **params;       /* 函数宏参数名（对象宏 NULL） */
    int nparams;         /* 函数宏参数个数；对象宏 -1 */
    Token *body;         /* 宏体 token 链（NULL 结尾；NULL = 空体） */
    int expanding;       /* 防自展开 */
} Macro;

static Macro *macros;
static int pp_depth;       /* #include 嵌套深度（<64） */

static Macro *find_macro(const char *name, int len) {
    for (Macro *m = macros; m; m = m->next)
        if (m->len == len && strncmp(m->name, name, (size_t)len) == 0)
            return m;
    return NULL;
}

static void add_macro(Macro *m) {
    /* 同名重定义：先移除旧定义（覆盖，不检查） */
    Macro **pp = &macros;
    while (*pp) {
        if ((*pp)->len == m->len && strncmp((*pp)->name, m->name, (size_t)m->len) == 0) {
            *pp = (*pp)->next;   /* 旧定义泄漏（工具编译器，无所谓） */
            break;
        }
        pp = &(*pp)->next;
    }
    m->next = macros;
    macros = m;
}

static void del_macro(const char *name, int len) {
    Macro **pp = &macros;
    while (*pp) {
        if ((*pp)->len == len && strncmp((*pp)->name, name, (size_t)len) == 0) {
            *pp = (*pp)->next;
            return;
        }
        pp = &(*pp)->next;
    }
}

/* ---------- token 工具 ---------- */

/* 复制 [head..tail]（含 tail）为独立链（NULL 结尾）。浅拷贝：共享 loc/str。 */
static Token *copy_range(Token *head, Token *tail) {
    if (!head)
        return NULL;
    Token dh = {0};
    Token *cur = &dh;
    for (Token *t = head;; t = t->next) {
        Token *n = (Token *)calloc(1, sizeof(Token));
        if (!n) { fprintf(stderr, "out of memory\n"); exit(1); }
        *n = *t;
        n->next = NULL;
        cur = cur->next = n;
        if (t == tail) break;
    }
    return dh.next;
}

static Token *tok_dup(const Token *t) {
    Token *n = (Token *)calloc(1, sizeof(Token));
    if (!n) { fprintf(stderr, "out of memory\n"); exit(1); }
    *n = *t;
    n->next = NULL;
    return n;
}

static Token *tok_new(int kind) {
    Token *t = (Token *)calloc(1, sizeof(Token));
    if (!t) { fprintf(stderr, "out of memory\n"); exit(1); }
    t->kind = kind;
    return t;
}

/* 合成数字 token（#if 表达式/defined 替换用） */
static Token *new_num(int64_t val) {
    Token *t = tok_new(TK_NUM);
    t->val = val;
    return t;
}

/* 返回 token 的源拼写（字符串字面量含引号；合成字符串无 loc → str 字节） */
static const char *tok_spell(const Token *t, int *len_out) {
    if (t->kind == TK_STR && t->len == 0) {
        *len_out = t->str_len;
        return t->str;
    }
    *len_out = t->len;
    return t->loc;
}

static Token *chain_tail(Token *t) {
    if (!t)
        return NULL;
    while (t->next)
        t = t->next;
    return t;
}

/* 返回 t 所在行的最后一个 token（下一个 at_bol token 的前一个；t 须为本行 token） */
static Token *line_end(Token *t) {
    while (t->next->kind != TK_EOF && !tok_at_bol(t->next))
        t = t->next;
    return t;
}

static bool tok_ident(const Token *t) {
    return t && (t->kind == TK_IDENT || t->kind == TK_KEYWORD);
}

/* 按文本比较（IDENT/KEYWORD/PUNCT 均可；tok_is 只认标点/关键字） */
static bool tok_name_is(const Token *t, const char *s) {
    return t && t->kind != TK_EOF &&
           t->len == (int)strlen(s) &&
           strncmp(t->loc, s, (size_t)t->len) == 0;
}

/* ---------- 链尾追加（## 需要弹出链尾，故同时维护倒数第二个节点） ---------- */

static void append1(Token **out, Token **out2, Token *t) {
    t->next = NULL;
    (*out)->next = t;
    *out2 = *out;
    *out = t;
}

static void append_list(Token **out, Token **out2, Token *e) {
    if (!e)
        return;
    (*out)->next = e;
    Token *last = e;
    Token *slast = *out;
    while (last->next) {
        slast = last;
        last = last->next;
    }
    *out = last;
    *out2 = slast;
}

/* ---------- 宏展开 ---------- */

static int param_index(Macro *m, const Token *t) {
    for (int i = 0; i < m->nparams; i++)
        if ((int)strlen(m->params[i]) == t->len &&
            strncmp(m->params[i], t->loc, (size_t)t->len) == 0)
            return i;
    return -1;
}

static bool is_param(Macro *m, const Token *t) {
    return m->nparams >= 0 && t &&
           (t->kind == TK_IDENT || t->kind == TK_KEYWORD) && param_index(m, t) >= 0;
}

static Token *expand_range(Token *head, Token *tail);

/* 宏实参：[head..tail]（tail = 实参最后一个 token；空实参 head=tail=NULL）。
 * 实参是源 token 流中的连续段，必须用 head/tail 界定，不能靠链尾
 * （源流中实参之后还有别的 token）。 */
typedef struct {
    Token *head;
    Token *tail;
} Arg;

/* 收集函数宏实参：cur 指向 '(' 之后。括号匹配，深度 0 的逗号分割，允许空实参。
 * 跨行（at_bol）/EOF 未闭合 → exit(1)。
 * 返回 args 数组（元素为实参 head/tail）；n_out = 实参数
 * （紧随 '(' 的 ')' → 0 个实参）；*after_out = ')' 之后的 token。 */
static Arg *collect_args(Token *cur, int *n_out, Token **after_out) {
    int ncap = 8;
    Arg *args = (Arg *)xmalloc((size_t)ncap * sizeof(Arg));
    int n = 0;
    Arg cur_arg = {NULL, NULL};
    Token *t = cur;
    int depth = 0;
    for (;;) {
        if (t->kind == TK_EOF || tok_at_bol(t)) {
            fprintf(stderr, "unterminated macro argument list\n");
            exit(1);
        }
        if (tok_is(t, "(")) {
            depth++;
        } else if (tok_is(t, ")")) {
            if (depth == 0) {
                if (n == 0 && !cur_arg.head) {
                    *n_out = 0;                 /* F()：0 个实参 */
                } else {
                    args[n++] = cur_arg;        /* 末实参（可为空） */
                    *n_out = n;
                }
                *after_out = t->next;
                return args;
            }
            depth--;
        } else if (tok_is(t, ",") && depth == 0) {
            args[n++] = cur_arg;
            if (n == ncap) {
                ncap *= 2;
                args = (Arg *)realloc(args, (size_t)ncap * sizeof(Arg));
            }
            cur_arg = (Arg){NULL, NULL};
            t = t->next;
            continue;
        }
        if (!cur_arg.head)
            cur_arg.head = t;
        cur_arg.tail = t;
        t = t->next;
    }
}

/* 实参全部 token 拼写直接相接（无空格），malloc 返回。head==NULL → 空串。 */
static char *arg_spell(Token *arg, Token *arg_tail, int *len_out) {
    size_t cap = 64;
    char *buf = (char *)xmalloc(cap);
    int n = 0;
    for (Token *t = arg; t; t = t->next) {
        const char *s;
        int l;
        s = tok_spell(t, &l);
        if ((size_t)n + (size_t)l + 1 > cap) {
            while ((size_t)n + (size_t)l + 1 > cap) cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        memcpy(buf + n, s, (size_t)l);
        n += l;
        if (t == arg_tail)
            break;
    }
    buf[n] = 0;
    *len_out = n;
    return buf;
}

/* # 字符串化：实参的拼写（token 间单个空格），转义 " 与 \。head==NULL → "" */
static Token *stringize(Token *arg, Token *arg_tail) {
    size_t cap = 64;
    char *buf = (char *)xmalloc(cap);
    int n = 0;
    for (Token *t = arg; t; t = t->next) {
        const char *s;
        int l;
        s = tok_spell(t, &l);
        for (int i = 0; i < l; i++) {
            if ((size_t)n + 3 > cap) {
                cap *= 2;
                buf = (char *)realloc(buf, cap);
            }
            if (s[i] == '"' || s[i] == '\\')
                buf[n++] = '\\';
            buf[n++] = s[i];
        }
        if (t != arg_tail)
            buf[n++] = ' ';
        if (t == arg_tail)
            break;
    }
    buf[n] = 0;
    Token *st = tok_new(TK_STR);
    st->str = buf;
    st->str_len = n;
    st->len = 0;      /* 合成 token：无源拼写 */
    return st;
}

/* 宏体替换：参数 → 展开后的实参；# 参数 → 字符串化；A ## B → 粘贴后重新
 * tokenize。返回新链（NULL 结尾）。对象宏 args 为 NULL。
 * 参数与 ## 相邻时用 raw 实参（未展开）。 */
static Token *substitute(Macro *m, Arg *args) {
    Token dh = {0};
    Token *out = &dh;    /* 链尾 */
    Token *out2 = &dh;   /* 链尾前一个（## 弹出用） */
    Token *t = m->body;
    while (t) {
        if (tok_is(t, "##")) {
            /* A ## B：A = 输出链尾，B = 下一个模板 token（参数 → raw 实参） */
            if (out == &dh) {
                fprintf(stderr, "'##' at beginning of macro body\n");
                exit(1);
            }
            Token *nxt = t->next;
            if (!nxt) {
                fprintf(stderr, "'##' at end of macro body\n");
                exit(1);
            }
            Token *prev = out;
            out = out2;
            out->next = NULL;      /* 弹出 A */
            const char *a;
            int al;
            a = tok_spell(prev, &al);
            const char *b;
            int bl;
            char *bfree = NULL;
            if (is_param(m, nxt)) {
                Arg ag = args[param_index(m, nxt)];
                bfree = arg_spell(ag.head, ag.tail, &bl);
                b = bfree;
            } else {
                b = tok_spell(nxt, &bl);
            }
            char *buf = (char *)xmalloc((size_t)al + (size_t)bl + 1);
            memcpy(buf, a, (size_t)al);
            memcpy(buf + al, b, (size_t)bl);
            buf[al + bl] = 0;
            Token *merged = tokenize(buf);
            Token *mh = (merged->kind == TK_EOF) ? NULL : merged;
            append_list(&out, &out2, mh);   /* 空拼接 → 无 token */
            t = nxt->next;
            continue;
        }
        if (m->nparams >= 0 && tok_is(t, "#") && t->next && is_param(m, t->next)) {
            Arg ag = args[param_index(m, t->next)];
            append1(&out, &out2, stringize(ag.head, ag.tail));
            t = t->next->next;
            continue;
        }
        if (m->nparams >= 0 && is_param(m, t)) {
            Arg ag = args[param_index(m, t)];
            if (t->next && tok_is(t->next, "##")) {
                /* 与 ## 相邻（A 侧）：用 raw 实参 */
                if (ag.head)
                    append_list(&out, &out2, copy_range(ag.head, ag.tail));
            } else {
                /* 普通参数：实参整体展开后替换 */
                if (ag.head) {
                    Token *e = expand_range(ag.head, ag.tail);
                    append_list(&out, &out2, e);
                }
            }
            t = t->next;
            continue;
        }
        append1(&out, &out2, tok_dup(t));
        t = t->next;
    }
    return dh.next;
}

/* 展开 [head..tail] 内的宏（含函数宏调用），返回新链。
 * 自展开保护：m->expanding 置位时原样输出。
 * 函数宏实参跨行/EOF 未闭合 → exit(1)。 */
static Token *expand_range(Token *head, Token *tail) {
    if (!head)
        return NULL;
    Token dh = {0};
    Token *out = &dh, *out2 = &dh;
    Token *t = head;
    while (t) {
        if (t == tail->next)
            break;
        Token *nx = t->next;
        Macro *m = (t->kind == TK_IDENT || t->kind == TK_KEYWORD)
                       ? find_macro(t->loc, t->len) : NULL;
        if (m && !m->expanding) {
            if (m->nparams < 0) {
                /* 对象宏 */
                m->expanding = 1;
                Token *s = substitute(m, NULL);
                Token *e = expand_range(s, chain_tail(s));
                append_list(&out, &out2, e);
                m->expanding = 0;
            } else if (nx && tok_is(nx, "(")) {
                /* 函数宏 */
                Arg *args;
                int na;
                Token *after;
                args = collect_args(nx->next, &na, &after);
                if (na != m->nparams) {
                    fprintf(stderr, "macro '%s' expects %d arguments, but %d given\n",
                            m->name, m->nparams, na);
                    exit(1);
                }
                m->expanding = 1;
                Token *s = substitute(m, args);
                Token *e = expand_range(s, chain_tail(s));
                append_list(&out, &out2, e);
                m->expanding = 0;
                t = after;
                continue;
            } else {
                append1(&out, &out2, tok_dup(t));
            }
        } else {
            append1(&out, &out2, tok_dup(t));
        }
        t = nx;
    }
    return dh.next;
}

/* ---------- #if 常量表达式求值（递归下降） ---------- */

typedef struct {
    Token *cur;
    Token *end;    /* 表达式最后一个 token（end->next 视作越界） */
} Eval;

static bool at_end(Eval *c) {
    return c->cur == c->end->next;
}

static int64_t eval_expr(Eval *c);

static void expect(Eval *c, const char *s) {
    if (!at_end(c) && tok_is(c->cur, s)) {
        c->cur = c->cur->next;
        return;
    }
    fprintf(stderr, "expected '%s' in #if expression\n", s);
    exit(1);
}

static int64_t eval_primary(Eval *c) {
    if (at_end(c)) {
        fprintf(stderr, "unexpected end of #if expression\n");
        exit(1);
    }
    Token *t = c->cur;
    if (t->kind == TK_NUM) {
        c->cur = t->next;
        return t->val;
    }
    if (t->kind == TK_IDENT || t->kind == TK_KEYWORD) {
        c->cur = t->next;      /* 未定义标识符 = 0 */
        return 0;
    }
    if (tok_is(t, "(")) {
        c->cur = t->next;
        int64_t v = eval_expr(c);
        expect(c, ")");
        return v;
    }
    fprintf(stderr, "unexpected token in #if expression\n");
    exit(1);
}

static int64_t eval_unary(Eval *c) {
    if (tok_is(c->cur, "+")) {
        c->cur = c->cur->next;
        return eval_unary(c);
    }
    if (tok_is(c->cur, "-")) {
        c->cur = c->cur->next;
        return -eval_unary(c);
    }
    if (tok_is(c->cur, "~")) {
        c->cur = c->cur->next;
        return ~eval_unary(c);
    }
    if (tok_is(c->cur, "!")) {
        c->cur = c->cur->next;
        return !eval_unary(c);
    }
    return eval_primary(c);
}

static int64_t eval_mul(Eval *c) {
    int64_t v = eval_unary(c);
    while (!at_end(c) &&
           (tok_is(c->cur, "*") || tok_is(c->cur, "/") || tok_is(c->cur, "%"))) {
        bool d = tok_is(c->cur, "/");
        bool m = tok_is(c->cur, "%");
        c->cur = c->cur->next;
        int64_t r = eval_unary(c);
        if (d)
            v = r ? v / r : 0;            /* 除零 → 0（C 标准未定义，取 0） */
        else if (m)
            v = r ? v % r : 0;
        else
            v = v * r;
    }
    return v;
}

static int64_t eval_add(Eval *c) {
    int64_t v = eval_mul(c);
    while (!at_end(c) && (tok_is(c->cur, "+") || tok_is(c->cur, "-"))) {
        bool is_add = tok_is(c->cur, "+");
        c->cur = c->cur->next;
        int64_t r = eval_mul(c);
        v = is_add ? v + r : v - r;
    }
    return v;
}

static int64_t eval_shift(Eval *c) {
    int64_t v = eval_add(c);
    while (!at_end(c) && (tok_is(c->cur, "<<") || tok_is(c->cur, ">>"))) {
        bool is_shl = tok_is(c->cur, "<<");
        c->cur = c->cur->next;
        int64_t r = eval_add(c);
        v = is_shl ? v << (r & 63) : v >> (r & 63);
    }
    return v;
}

static int64_t eval_rel(Eval *c) {
    int64_t v = eval_shift(c);
    while (!at_end(c) &&
           (tok_is(c->cur, "<") || tok_is(c->cur, "<=") ||
            tok_is(c->cur, ">") || tok_is(c->cur, ">="))) {
        bool lt = tok_is(c->cur, "<");
        bool le = tok_is(c->cur, "<=");
        bool gt = tok_is(c->cur, ">");
        c->cur = c->cur->next;
        int64_t r = eval_shift(c);
        if (lt) v = v < r;
        else if (le) v = v <= r;
        else if (gt) v = v > r;
        else v = v >= r;
    }
    return v;
}

static int64_t eval_eq(Eval *c) {
    int64_t v = eval_rel(c);
    while (!at_end(c) && (tok_is(c->cur, "==") || tok_is(c->cur, "!="))) {
        bool is_eq = tok_is(c->cur, "==");
        c->cur = c->cur->next;
        int64_t r = eval_rel(c);
        v = is_eq ? (v == r) : (v != r);
    }
    return v;
}

static int64_t eval_bitand(Eval *c) {
    int64_t v = eval_eq(c);
    while (!at_end(c) && tok_is(c->cur, "&")) {
        c->cur = c->cur->next;
        v &= eval_eq(c);
    }
    return v;
}

static int64_t eval_bitxor(Eval *c) {
    int64_t v = eval_bitand(c);
    while (!at_end(c) && tok_is(c->cur, "^")) {
        c->cur = c->cur->next;
        v ^= eval_bitand(c);
    }
    return v;
}

static int64_t eval_bitor(Eval *c) {
    int64_t v = eval_bitxor(c);
    while (!at_end(c) && tok_is(c->cur, "|")) {
        c->cur = c->cur->next;
        v |= eval_bitxor(c);
    }
    return v;
}

static int64_t eval_and(Eval *c) {
    int64_t v = eval_bitor(c);
    while (!at_end(c) && tok_is(c->cur, "&&")) {
        c->cur = c->cur->next;
        int64_t r = eval_bitor(c);
        v = v && r;      /* 两侧全算；除零→0，结果与短路一致 */
    }
    return v;
}

static int64_t eval_or(Eval *c) {
    int64_t v = eval_and(c);
    while (!at_end(c) && tok_is(c->cur, "||")) {
        c->cur = c->cur->next;
        int64_t r = eval_and(c);
        v = v || r;
    }
    return v;
}

static int64_t eval_expr(Eval *c) {
    int64_t v = eval_or(c);
    if (!at_end(c) && tok_is(c->cur, "?")) {
        c->cur = c->cur->next;
        int64_t then = eval_expr(c);
        expect(c, ":");
        int64_t els = eval_expr(c);
        return v ? then : els;
    }
    return v;
}

/* 求值表达式链 [head..tail]（已宏展开、defined 已替换） */
static int64_t eval_chain(Token *head, Token *tail) {
    Eval c = {head, tail};
    int64_t v = eval_expr(&c);
    if (!at_end(&c)) {
        fprintf(stderr, "extra tokens in #if expression\n");
        exit(1);
    }
    return v;
}

/* #if 行预处理：把 defined X / defined(X) 替换为 0/1（宏展开之前）。
 * 返回新链（NULL 结尾）。 */
static Token *pre_defs(Token *head, Token *tail) {
    Token dh = {0};
    Token *cur = &dh;
    Token *t = head;
    while (1) {
        Token *nx = t->next;
        if (t->kind == TK_IDENT && t->len == 7 && strncmp(t->loc, "defined", 7) == 0) {
            if (nx && tok_is(nx, "(")) {
                Token *inner = nx->next;
                if (inner && tok_ident(inner) && tok_is(inner->next, ")")) {
                    int v = find_macro(inner->loc, inner->len) ? 1 : 0;
                    cur = cur->next = new_num(v);
                    t = inner->next->next;
                    if (t == tail->next)
                        break;
                    continue;
                }
                fprintf(stderr, "malformed defined() in #if expression\n");
                exit(1);
            }
            if (nx && tok_ident(nx)) {
                int v = find_macro(nx->loc, nx->len) ? 1 : 0;
                cur = cur->next = new_num(v);
                t = nx->next;
                if (t == tail->next)
                    break;
                continue;
            }
            fprintf(stderr, "malformed 'defined' in #if expression\n");
            exit(1);
        }
        cur = cur->next = tok_dup(t);
        if (t == tail)
            break;
        t = nx;
    }
    return dh.next;
}

/* ---------- 条件编译栈 ---------- */

static int cond_skip[64];    /* 本层是否跳过（1=跳过，不输出） */
static int cond_taken[64];   /* 本层是否已取真分支 */
static int cond_depth;

static void cond_push(int skip, int taken) {
    if (cond_depth >= 64) {
        fprintf(stderr, "#if nesting too deep\n");
        exit(1);
    }
    cond_skip[cond_depth] = skip;
    cond_taken[cond_depth] = taken;
    cond_depth++;
}

static bool skipping(void) {
    for (int i = 0; i < cond_depth; i++)
        if (cond_skip[i])
            return true;
    return false;
}

/* 第 level 层之前的层是否跳过（#elif 表达式展开判定用） */
static bool skipping_below(int level) {
    for (int i = 0; i < level; i++)
        if (cond_skip[i])
            return true;
    return false;
}

/* ---------- 指令处理 ---------- */

static void do_define(Token *t) {
    if (!tok_ident(t)) {
        fprintf(stderr, "macro name expected after #define\n");
        exit(1);
    }
    Macro *m = (Macro *)calloc(1, sizeof(Macro));
    if (!m) { fprintf(stderr, "out of memory\n"); exit(1); }
    m->name = xstrndup(t->loc, t->len);
    m->len = t->len;
    Token *name = t;
    t = name->next;
    /* 函数宏：'(' 紧跟名字之后（无空白——用 loc 相邻判定） */
    if (t && tok_is(t, "(") && t->loc == name->loc + name->len) {
        int ncap = 8;
        m->params = (char **)xmalloc((size_t)ncap * sizeof(char *));
        t = t->next;
        if (tok_is(t, ")")) {
            m->nparams = 0;
            t = t->next;
        } else {
            for (;;) {
                if (!tok_ident(t)) {
                    fprintf(stderr, "macro parameter expected\n");
                    exit(1);
                }
                if (m->nparams == ncap) {
                    ncap *= 2;
                    m->params = (char **)realloc(m->params, (size_t)ncap * sizeof(char *));
                }
                m->params[m->nparams++] = xstrndup(t->loc, t->len);
                t = t->next;
                if (tok_is(t, ",")) {
                    t = t->next;
                    continue;
                }
                if (tok_is(t, ")")) {
                    t = t->next;
                    break;
                }
                fprintf(stderr, "expected ',' or ')' in macro parameter list\n");
                exit(1);
            }
        }
    } else {
        m->nparams = -1;
    }
    /* body：本行剩余 token（不含下一个 at_bol 的 token） */
    if (t->kind == TK_EOF || tok_at_bol(t))
        m->body = NULL;
    else
        m->body = copy_range(t, line_end(t));
    add_macro(m);
}

static void do_undef(Token *t) {
    if (!tok_ident(t)) {
        fprintf(stderr, "macro name expected after #undef\n");
        exit(1);
    }
    del_macro(t->loc, t->len);
}

/* 尝试 dir/fname；可打开则返回 malloc 路径，否则 NULL */
static char *try_path(const char *dir, int dlen, const char *fname, int flen) {
    char *p = (char *)xmalloc((size_t)dlen + (size_t)flen + 2);
    if (dlen > 0) {
        memcpy(p, dir, (size_t)dlen);
        p[dlen] = '/';
        memcpy(p + dlen + 1, fname, (size_t)flen);
        p[dlen + 1 + flen] = 0;
    } else {
        memcpy(p, fname, (size_t)flen);
        p[flen] = 0;
    }
    FILE *fp = fopen(p, "rb");
    if (fp) {
        fclose(fp);
        return p;
    }
    free(p);
    return NULL;
}

/* #include 解析："..." → 当前文件目录 → inc_dirs → cwd；"<...>" → 仅 inc_dirs */
static char *resolve_include(const char *fname, int flen, bool angle,
                             const char *src_name, const char **inc_dirs, int n_inc) {
    char *found;
    if (!angle && src_name) {
        const char *slash = strrchr(src_name, '/');
        const char *bslash = strrchr(src_name, '\\');
        const char *last = slash;
        if (!last || (bslash && bslash > last))
            last = bslash;
        if (last) {
            int dlen = (int)(last - src_name);
            found = try_path(src_name, dlen, fname, flen);
            if (found)
                return found;
        }
    }
    for (int i = 0; i < n_inc; i++) {
        found = try_path(inc_dirs[i], (int)strlen(inc_dirs[i]), fname, flen);
        if (found)
            return found;
    }
    if (!angle) {
        found = try_path(NULL, 0, fname, flen);   /* cwd */
        if (found)
            return found;
    }
    return NULL;
}

/* 返回递归展开的 include 结果链（不含 EOF；失败 exit(1)） */
static Token *pp_tokens(Token *tok, const char *src_name,
                        const char **inc_dirs, int n_inc,
                        const char **defines, int n_def, bool top_level);

static Token *do_include(Token *t, const char *src_name,
                         const char **inc_dirs, int n_inc) {
    bool angle = false;
    const char *fname;
    int flen;
    if (t->kind == TK_STR) {
        fname = t->str;
        flen = t->str_len;
    } else if (tok_is(t, "<")) {
        angle = true;
        Token *gt = t->next;
        while (gt && gt->kind != TK_EOF && !tok_at_bol(gt) && !tok_is(gt, ">"))
            gt = gt->next;
        if (!gt || !tok_is(gt, ">")) {
            fprintf(stderr, "#include expects '>'\n");
            exit(1);
        }
        /* 原文切片：'<' 与 '>' 之间（tokenize 已保证可词法化） */
        fname = t->next->loc;
        flen = (int)(gt->loc - fname);
    } else {
        fprintf(stderr, "#include expects \"file\" or <file>\n");
        exit(1);
    }
    char *fpath = resolve_include(fname, flen, angle, src_name, inc_dirs, n_inc);
    if (!fpath) {
        char *nm = xstrndup(fname, flen);
        fprintf(stderr, "cannot open include %s\n", nm);
        exit(1);
    }
    if (pp_depth >= 64) {
        fprintf(stderr, "#include nested too deeply\n");
        exit(1);
    }
    pp_depth++;
    char *content = read_file(fpath);
    char *p12 = phase12(content);
    Token *itoks = tokenize(p12);
    Token *rtoks = pp_tokens(itoks, fpath, inc_dirs, n_inc, NULL, 0, false);
    pp_depth--;
    /* 注意：p12/content 不释放——rtoks 与包含文件中 #define 的宏体
     * token 的 loc 均指向 p12，可能在 include 返回后（主文件展开时）
     * 才被读取。工具编译器按进程生命周期泄漏，可接受。 */
    if (!rtoks || rtoks->kind == TK_EOF)
        return NULL;
    /* 去掉结尾 EOF（结果要拼接到主输出流中间） */
    Token *p = rtoks;
    while (p->next && p->next->kind != TK_EOF)
        p = p->next;
    p->next = NULL;
    return rtoks;
}

static void do_cond_if(Token *t, int kind) {
    /* kind: 0=#if 1=#ifdef 2=#ifndef；t = 指令名后第一个 token */
    int v;
    if (kind == 1 || kind == 2) {
        if (!tok_ident(t)) {
            fprintf(stderr, kind == 1 ? "#ifdef expects a macro name\n"
                                      : "#ifndef expects a macro name\n");
            exit(1);
        }
        int d = find_macro(t->loc, t->len) != NULL;
        v = (kind == 1) ? d : !d;
    } else {
        if (t->kind == TK_EOF || tok_at_bol(t)) {
            fprintf(stderr, "#if with no expression\n");
            exit(1);
        }
        Token *tail = line_end(t);
        Token *ex = pre_defs(t, tail);
        if (!skipping())
            ex = expand_range(ex, chain_tail(ex));
        v = (int)eval_chain(ex, chain_tail(ex));
    }
    cond_push(!v, v != 0);
}

static void do_elif(Token *t) {
    if (cond_depth == 0) {
        fprintf(stderr, "#elif without #if\n");
        exit(1);
    }
    int lv = cond_depth - 1;
    if (!cond_taken[lv]) {
        int v;
        if (t->kind == TK_EOF || tok_at_bol(t)) {
            v = 1;   /* 裸 #elif 视为 #elif 1（gcc 早期扩展；Linux 0.11 blk.h 用到） */
        } else {
            Token *tail = line_end(t);
            Token *ex = pre_defs(t, tail);
            if (!skipping_below(lv))
                ex = expand_range(ex, chain_tail(ex));
            v = (int)eval_chain(ex, chain_tail(ex));
        }
        cond_taken[lv] = (v != 0);
        cond_skip[lv] = !v;
    } else {
        cond_skip[lv] = 1;   /* 已取真分支：本层后续 #elif/#else 均跳过 */
    }
}

static void do_else(void) {
    if (cond_depth == 0) {
        fprintf(stderr, "#else without #if\n");
        exit(1);
    }
    int lv = cond_depth - 1;
    if (cond_taken[lv])
        cond_skip[lv] = 1;
    else {
        cond_skip[lv] = 0;
        cond_taken[lv] = 1;
    }
}

/* -D 参数入表：defines[i] = "NAME" 或 "NAME=VALUE" */
static void apply_defines(const char **defines, int n_def) {
    for (int i = 0; i < n_def; i++) {
        const char *s = defines[i];
        const char *eq = strchr(s, '=');
        int nlen = eq ? (int)(eq - s) : (int)strlen(s);
        if (nlen == 0) {
            fprintf(stderr, "empty -D macro name\n");
            exit(1);
        }
        Macro *m = (Macro *)calloc(1, sizeof(Macro));
        if (!m) { fprintf(stderr, "out of memory\n"); exit(1); }
        m->name = xstrndup(s, nlen);
        m->len = nlen;
        m->nparams = -1;
        if (eq) {
            Token *btoks = tokenize(eq + 1);
            if (btoks->kind != TK_EOF) {
                Token *p = btoks;
                while (p->next && p->next->kind != TK_EOF)
                    p = p->next;
                p->next = NULL;
                m->body = btoks;
            }
        }
        add_macro(m);
    }
}

/* ---------- 主循环 ---------- */

static Token *pp_tokens(Token *tok, const char *src_name,
                        const char **inc_dirs, int n_inc,
                        const char **defines, int n_def, bool top_level) {
    if (top_level) {
        /* 每次顶层调用独立（测试间互不影响）；递归调用共享宏表 */
        macros = NULL;
        pp_depth = 0;
        cond_depth = 0;
        apply_defines(defines, n_def);
    }

    Token head = {0};
    Token *out = &head, *out2 = &head;
    Token *t = tok;
    while (t->kind != TK_EOF) {
        if (tok_at_bol(t) && tok_is(t, "#")) {
            Token *name = t->next;
            if (name->kind == TK_EOF) {
                fprintf(stderr, "unexpected end of file in directive\n");
                exit(1);
            }
            if (tok_at_bol(name)) {
                /* 空指令：'#' 单独一行，无操作 */
                t = name;
                continue;
            }
            if (tok_name_is(name, "define")) {
                if (!skipping())
                    do_define(name->next);
                t = line_end(name)->next;
            } else if (tok_name_is(name, "undef")) {
                if (!skipping())
                    do_undef(name->next);
                t = line_end(name)->next;
            } else if (tok_name_is(name, "include")) {
                if (!skipping()) {
                    Token *inc = do_include(name->next, src_name, inc_dirs, n_inc);
                    append_list(&out, &out2, inc);
                }
                t = line_end(name)->next;
            } else if (tok_name_is(name, "if") || tok_name_is(name, "ifdef") ||
                       tok_name_is(name, "ifndef")) {
                int kind = tok_name_is(name, "if") ? 0
                         : (tok_name_is(name, "ifdef") ? 1 : 2);
                do_cond_if(name->next, kind);
                t = line_end(name)->next;
            } else if (tok_name_is(name, "elif")) {
                do_elif(name->next);
                t = line_end(name)->next;
            } else if (tok_name_is(name, "else")) {
                do_else();
                t = line_end(name)->next;
            } else if (tok_name_is(name, "endif")) {
                if (cond_depth == 0) {
                    fprintf(stderr, "#endif without #if\n");
                    exit(1);
                }
                cond_depth--;
                t = line_end(name)->next;
            } else {
                if (!skipping()) {
                    fprintf(stderr, "unknown preprocessor directive: #%.*s\n",
                            name->len, name->loc);
                    exit(1);
                }
                t = line_end(name)->next;
            }
            continue;
        }
        /* 普通行：整行宏展开后输出 */
        Token *tail = line_end(t);
        if (!skipping()) {
            Token *e = expand_range(t, tail);
            append_list(&out, &out2, e);
        }
        t = tail->next;
    }
    append1(&out, &out2, tok_dup(t));   /* 结尾 EOF token */
    return head.next;
}

Token *preprocess_tokens(Token *tok, const char *src_name,
                         const char **inc_dirs, int n_inc,
                         const char **defines, int n_def) {
    return pp_tokens(tok, src_name, inc_dirs, n_inc, defines, n_def, true);
}

/* ---------- 文本重建（-E 输出） ---------- */

/* 除 "(" 之后 / ")" 之前外一律加空格。理由：简单可预测，且保证
 * "x = 42"、"((1) + (2))" 这类形态（测试依赖）；重新 tokenize 后
 * token 序列不变（"(" 与 ")" 之间不会拼接出新 token）。 */
static bool needs_space(const Token *a, const Token *b) {
    if (tok_is(a, "(") || tok_is(b, ")"))
        return false;
    return true;
}

static void grow(char **buf, size_t *cap, size_t need) {
    if (need > *cap) {
        while (need > *cap)
            *cap *= 2;
        *buf = (char *)realloc(*buf, *cap);
    }
}

static char *unparse(Token *toks) {
    size_t cap = 256;
    char *buf = (char *)xmalloc(cap);
    int n = 0;
    Token *prev = NULL;
    for (Token *t = toks; t && t->kind != TK_EOF; t = t->next) {
        if (prev && needs_space(prev, t))
            buf[n++] = ' ';
        switch (t->kind) {
        case TK_NUM: {
            char tmp[40];
            int l = sprintf(tmp, "%lld", (long long)t->val);
            if (t->is_unsigned)
                tmp[l++] = 'u';
            grow(&buf, &cap, (size_t)n + (size_t)l + 1);
            memcpy(buf + n, tmp, (size_t)l);
            n += l;
            break;
        }
        case TK_STR: {
            grow(&buf, &cap, (size_t)n + (size_t)t->str_len * 2 + 3);
            buf[n++] = '"';
            for (int i = 0; i < t->str_len; i++) {
                if (t->str[i] == '"' || t->str[i] == '\\')
                    buf[n++] = '\\';
                else if (t->str[i] == '\0') {
                    buf[n++] = '\\';
                    buf[n++] = '0';
                } else {
                    buf[n++] = t->str[i];
                }
            }
            buf[n++] = '"';
            break;
        }
        default:
            grow(&buf, &cap, (size_t)n + (size_t)t->len + 1);
            memcpy(buf + n, t->loc, (size_t)t->len);
            n += t->len;
            break;
        }
        prev = t;
    }
    buf[n] = 0;
    return buf;
}

/* phase12（续行合并+注释剥离）→ tokenize → preprocess_tokens 一站式入口。
 * 返回链的 loc 指向内部 malloc 缓冲（不随函数返回释放——调用方须在
 * 整个预处理/解析过程保持使用；按进程生命周期泄漏，工具编译器可接受）。 */
Token *preprocess_source_text(const char *src, const char *src_name,
                              const char **inc_dirs, int n_inc,
                              const char **defines, int n_def) {
    char *p12 = phase12(src);
    Token *toks = tokenize(p12);
    return preprocess_tokens(toks, src_name, inc_dirs, n_inc, defines, n_def);
}

char *preprocess_text(const char *src, const char *src_name,
                      const char **inc_dirs, int n_inc,
                      const char **defines, int n_def) {
    char *p12 = phase12(src);
    Token *toks = tokenize(p12);
    Token *pp = preprocess_tokens(toks, src_name, inc_dirs, n_inc, defines, n_def);
    char *out = unparse(pp);
    free(p12);
    return out;
}
