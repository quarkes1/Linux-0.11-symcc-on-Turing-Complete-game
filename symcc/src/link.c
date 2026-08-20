/* symcc/src/link.c — symld 链接器核心
 *
 * 输入：N 个 .sym 可重定位对象 + crt0 模板（obj[0]，仅 text 行）。
 * 布局：text 从地址 0 → data（基址 4 对齐）→ bss（基址 4 对齐）。
 * 引用解析（Task 5 引用形态）：
 *   @name          D16：全局符号 → 绝对地址；否则当前对象段内标签
 *                  → 段基址 + 段内偏移（>0xFFFF → 提示 --d32）
 *   @hi:name       D32 高半字（无范围检查）
 *   @lo:name       D32 低半字
 *   call/jmp/条件跳转 name（J16）：全局函数 → 保留 label；否则当前
 *                  对象段内标签 → 数字；未定义 → 错误
 * 输出：绝对地址 asm（数据引用全数字；仅全局函数保留标签行与引用；
 * L/s/static 标签解析为数字、定义行丢弃）；末尾 halt: jmp halt。
 * out_bin 非 NULL → asm_assemble 转二进制（与 emu 工具链同源）。
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj.h"
#include "link.h"
#include "emu/asm.h"
#include "symcc/include/config.h"

#define XSTR(x) STR(x)
#define STR(x) #x

/* ---------- 数据结构 ---------- */

/* 段内标签（L12 / s0 / static 函数 / static 全局 / crt0 L_bss_*） */
typedef struct LocalSym {
    struct LocalSym *next;
    char *name;
    int obj;        /* 所属对象（0 = crt0） */
    int segment;    /* 0=text, 1=data */
    long off;       /* 段内偏移 */
} LocalSym;

/* 全局符号（.sym 行 + crt0 内置 __bss_start/__bss_end） */
typedef struct GSymbol {
    struct GSymbol *next;
    char *name;
    bool is_data;
    int segment;
    int obj;
    long off;       /* 布局后绝对地址 */
    bool builtin;   /* __bss_start/__bss_end（布局时填） */
} GSymbol;

typedef struct Ctx {
    Obj **objs;
    int n;              /* 对象总数（crt0 = 0） */
    long text_size, data_base, data_size, bss_base, bss_total;
    long *text_off;     /* 每个对象的 text 绝对基址（对象内偏移 + 此基址） */
    long *data_off;     /* 每个对象的 data 绝对基址 */
    LocalSym *locals;
    GSymbol *globals;
    LinkError *err;
    bool failed;
} Ctx;

/* 动态输出缓冲（Windows 无 open_memstream） */
typedef struct Buf {
    char *p;
    size_t len, cap;
} Buf;

/* ---------- 工具 ---------- */

static void set_err(LinkError *err, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->msg, sizeof err->msg, fmt, ap);
    va_end(ap);
}

static long align4(long x) { return (x + 3) & ~3L; }

/* 条件跳转助记符（isa 表序） */
static const char *const cond_jumps[] =
    { "je", "jne", "jb", "jae", "jbe", "ja", "jl", "jge", "jle", "jg" };
static bool is_cond_jump(const char *w) {
    for (int i = 0; i < 10; i++)
        if (strcmp(w, cond_jumps[i]) == 0)
            return true;
    return false;
}

/* 寄存器名（jmp r1 等动态调用——不是符号引用） */
static bool is_reg_name(const char *w) {
    if (strcmp(w, "zr") == 0 || strcmp(w, "sp") == 0 || strcmp(w, "flags") == 0)
        return true;
    if (w[0] == 'r' && w[1] >= '1' && w[1] <= '9') {
        int v = 0;
        for (const char *p = w + 1; *p; p++) {
            if (*p < '0' || *p > '9')
                return false;
            v = v * 10 + (*p - '0');
        }
        return v >= 1 && v <= 13;
    }
    return false;
}

/* 符号名形态（J16 操作数：字母开头的标识符；数字/hex 不是） */
static bool is_sym_ident(const char *w) {
    if (!(isalpha((unsigned char)w[0]) || w[0] == '_'))
        return false;
    for (const char *p = w + 1; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_'))
            return false;
    return true;
}

static LocalSym *find_local(LocalSym *list, int obj, int segment, const char *name) {
    for (LocalSym *ls = list; ls; ls = ls->next)
        if (ls->obj == obj && ls->segment == segment &&
            strcmp(ls->name, name) == 0)
            return ls;
    return NULL;
}

static LocalSym *find_local_any(LocalSym *list, int obj, const char *name) {
    for (LocalSym *ls = list; ls; ls = ls->next)
        if (ls->obj == obj && strcmp(ls->name, name) == 0)
            return ls;
    return NULL;
}

static GSymbol *find_global(GSymbol *list, const char *name) {
    for (GSymbol *g = list; g; g = g->next)
        if (strcmp(g->name, name) == 0)
            return g;
    return NULL;
}

static void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        return;
    size_t n = (size_t)need;
    if (b->len + n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4096;
        while (b->len + n + 1 > b->cap)
            b->cap *= 2;
        b->p = (char *)realloc(b->p, b->cap);
        if (!b->p) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += n;
}

/* @name 解析：全局符号 → 绝对地址；否则当前对象段内标签 →
 * 对象段基址 + 段内偏移。未定义 → 写错误返回 false */
static bool resolve_dref(Ctx *c, int obj, const char *name, long *out) {
    GSymbol *g = find_global(c->globals, name);
    if (g) {
        *out = g->off;
        return true;
    }
    LocalSym *ls = find_local_any(c->locals, obj, name);
    if (ls) {
        *out = (ls->segment == 0 ? c->text_off[obj] : c->data_off[obj]) + ls->off;
        return true;
    }
    set_err(c->err, "undefined symbol: %s", name);
    return false;
}

/* ---------- 行重建（词级替换引用形态） ---------- */

static void emit_line(Ctx *c, int obj, int segment, const char *line, Buf *b) {
    if (c->failed)
        return;
    /* 标签行：仅导出的全局函数保留（引用已解析为数字）；
     * L/s/static 函数等定义行丢弃（无引用者） */
    size_t llen = obj_label_name_len(line);
    if (llen) {
        char *name = (char *)malloc(llen + 1);
        memcpy(name, line, llen);
        name[llen] = 0;
        GSymbol *g = find_global(c->globals, name);
        if (g && !g->is_data && g->segment == 0)
            buf_printf(b, "%s:\n", name);
        free(name);
        return;
    }
    /* 空行/纯注释/字符串数据行：原样 */
    const char *p = line;
    while (*p == ' ')
        p++;
    if (*p == 0 || *p == ';' || *p == '"') {
        buf_printf(b, "%s\n", line);
        return;
    }
    /* 指令/数据行：词级重建 */
    const char *rest = line;
    bool prev_jump = false;
    while (*rest) {
        while (*rest == ' ') {
            buf_printf(b, " ");
            rest++;
        }
        if (*rest == ';') {          /* 注释尾：原样保留 */
            buf_printf(b, "%s", rest);
            break;
        }
        const char *start = rest;
        while (*rest && *rest != ' ' && *rest != ';')
            rest++;
        char w[512];
        size_t wl = (size_t)(rest - start);
        if (wl >= sizeof w)
            wl = sizeof w - 1;
        memcpy(w, start, wl);
        w[wl] = 0;
        if (w[0] == '@') {
            long v = 0;
            if (strncmp(w, "@hi:", 4) == 0) {
                if (!resolve_dref(c, obj, w + 4, &v)) { c->failed = true; return; }
                buf_printf(b, "0x%04lx", (v >> 16) & 0xFFFF);
            } else if (strncmp(w, "@lo:", 4) == 0) {
                if (!resolve_dref(c, obj, w + 4, &v)) { c->failed = true; return; }
                buf_printf(b, "0x%04lx", v & 0xFFFF);
            } else {
                if (!resolve_dref(c, obj, w + 1, &v)) { c->failed = true; return; }
                if (v > 0xFFFF) {
                    set_err(c->err, "reference @%s = 0x%lx out of 16-bit range (use --d32)",
                            w + 1, v);
                    c->failed = true;
                    return;
                }
                buf_printf(b, "0x%lx", v);
            }
        } else if (prev_jump && is_sym_ident(w) && !is_reg_name(w)) {
            /* J16：全局函数保留 label；段内标签 → 数字 */
            GSymbol *g = find_global(c->globals, w);
            if (g) {
                buf_printf(b, "%s", w);
            } else {
                LocalSym *ls = find_local_any(c->locals, obj, w);
                if (!ls) {
                    set_err(c->err, "undefined symbol: %s", w);
                    c->failed = true;
                    return;
                }
                long v = (ls->segment == 0 ? c->text_off[obj] : c->data_off[obj]) + ls->off;
                if (v > 0xFFFF) {
                    set_err(c->err, "jump target %s = 0x%lx out of 16-bit range", w, v);
                    c->failed = true;
                    return;
                }
                buf_printf(b, "0x%lx", v);
            }
        } else {
            buf_printf(b, "%s", w);
        }
        prev_jump = is_cond_jump(w) || strcmp(w, "jmp") == 0 ||
                    strcmp(w, "call") == 0;
    }
    buf_printf(b, "\n");
    (void)segment;
}

/* ---------- 链接主流程 ---------- */

bool symld_link(Obj **objs_in, int n_in, const char *crt0_path,
                FILE *out_asm, FILE *out_bin, LinkError *err) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.n = n_in + 1;
    c.objs = (Obj **)calloc((size_t)c.n, sizeof(Obj *));
    c.err = err;
    if (!c.objs) {
        set_err(err, "out of memory");
        return false;
    }
    bool rc = false;

    /* crt0 = obj[0]（仅 text 行；FRAMEBUF_BASE 占位符 → config.h 值） */
    c.objs[0] = obj_new();
    FILE *f = fopen(crt0_path, "rb");
    if (!f) {
        set_err(err, "cannot open crt0: %s", crt0_path);
        goto done;
    }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = 0;
        const char *ph = "FRAMEBUF_BASE";
        const char *val = XSTR(FRAMEBUF_BASE);
        for (;;) {
            const char *hit = strstr(line, ph);
            if (!hit)
                break;
            char buf[1024];
            size_t pre = (size_t)(hit - line);
            snprintf(buf, sizeof buf, "%.*s%s%s", (int)pre, line, val,
                     hit + strlen(ph));
            strcpy(line, buf);
        }
        obj_add_text(c.objs[0], line);
    }
    fclose(f);
    for (int i = 0; i < n_in; i++)
        c.objs[i + 1] = objs_in[i];

    /* 布局：text 从 0 → data（4 对齐）→ bss（4 对齐）。
     * 每对象记录段绝对基址（text_off/data_off）：重定位 = 基址 + 段内偏移。
     * text_size 含 crt0 文本后插入的 4 字节 jmp halt（main 返回安全网）。 */
    c.text_off = (long *)calloc((size_t)c.n, sizeof(long));
    c.data_off = (long *)calloc((size_t)c.n, sizeof(long));
    if (!c.text_off || !c.data_off) {
        set_err(err, "out of memory");
        goto done;
    }
    long toff = 0;
    for (int oi = 0; oi < c.n; oi++) {
        c.text_off[oi] = toff;
        for (int i = 0; i < c.objs[oi]->n_text; i++)
            toff += obj_line_size(c.objs[oi]->text_lines[i]);
        if (oi == 0)
            toff += 4;      /* 插入的 jmp halt 位于 crt0 文本之后 */
    }
    c.text_size = toff;
    c.data_base = align4(toff);
    long doff = c.data_base;
    for (int oi = 0; oi < c.n; oi++) {
        c.data_off[oi] = doff;
        for (int i = 0; i < c.objs[oi]->n_data; i++)
            doff += obj_line_size(c.objs[oi]->data_lines[i]);
    }
    c.data_size = doff - c.data_base;
    /* halt 指令（4 字节）在二进制末尾 [text+data, +4) 处；bss 必须从
     * 其后开始，否则清零循环会覆盖 halt 代码 */
    c.bss_base = align4(doff + 4);
    for (int oi = 0; oi < c.n; oi++)
        for (ObjBss *b = c.objs[oi]->bss; b; b = b->next)
            c.bss_total += b->size;
    if (c.text_size + c.data_size > 0x10000) {
        set_err(err, "program exceeds 64KB (J16 limit): text+data = %ld bytes",
                c.text_size + c.data_size);
        goto done;
    }

    /* 段内标签表 + 全局符号表（重名检查） */
    for (int oi = 0; oi < c.n; oi++) {
        long off = 0;
        for (int i = 0; i < c.objs[oi]->n_text; i++) {
            const char *l = c.objs[oi]->text_lines[i];
            size_t ln = obj_label_name_len(l);
            if (ln) {
                LocalSym *ls = (LocalSym *)calloc(1, sizeof *ls);
                ls->name = (char *)malloc(ln + 1);
                memcpy(ls->name, l, ln);
                ls->name[ln] = 0;
                ls->obj = oi;
                ls->segment = 0;
                ls->off = off;
                ls->next = c.locals;
                c.locals = ls;
            }
            off += obj_line_size(l);
        }
        off = 0;
        for (int i = 0; i < c.objs[oi]->n_data; i++) {
            const char *l = c.objs[oi]->data_lines[i];
            size_t ln = obj_label_name_len(l);
            if (ln) {
                LocalSym *ls = (LocalSym *)calloc(1, sizeof *ls);
                ls->name = (char *)malloc(ln + 1);
                memcpy(ls->name, l, ln);
                ls->name[ln] = 0;
                ls->obj = oi;
                ls->segment = 1;
                ls->off = off;
                ls->next = c.locals;
                c.locals = ls;
            }
            off += obj_line_size(l);
        }
        for (ObjSymbol *s = c.objs[oi]->syms; s; s = s->next) {
            if (strcmp(s->name, "__bss_start") == 0 ||
                strcmp(s->name, "__bss_end") == 0) {
                set_err(err, "symbol name reserved: %s", s->name);
                goto done;
            }
            if (find_global(c.globals, s->name)) {
                set_err(err, "duplicate symbol: %s", s->name);
                goto done;
            }
            GSymbol *g = (GSymbol *)calloc(1, sizeof *g);
            g->name = strdup(s->name);
            g->is_data = s->is_data;
            g->segment = s->segment;
            g->obj = oi;
            g->next = c.globals;
            c.globals = g;
        }
    }

    /* 内置符号 + 全局符号绝对地址 */
    GSymbol *bstart = (GSymbol *)calloc(1, sizeof *bstart);
    bstart->name = strdup("__bss_start");
    bstart->builtin = true;
    bstart->off = c.bss_base;
    bstart->next = c.globals;
    c.globals = bstart;
    GSymbol *bend = (GSymbol *)calloc(1, sizeof *bend);
    bend->name = strdup("__bss_end");
    bend->builtin = true;
    /* 4 对齐：清零循环 4 字节步进，覆盖全部 bss（末尾溢出无害——bss 是
     * 程序尾部，其后无分配） */
    bend->off = align4(c.bss_base + c.bss_total);
    bend->next = c.globals;
    c.globals = bend;

    for (GSymbol *g = c.globals; g; g = g->next) {
        if (g->builtin)
            continue;
        if (g->segment == 0) {
            LocalSym *ls = find_local(c.locals, g->obj, 0, g->name);
            if (!ls) {
                set_err(err, "symbol %s: missing text label", g->name);
                goto done;
            }
            g->off = c.text_off[g->obj] + ls->off;
        } else {
            LocalSym *ls = find_local(c.locals, g->obj, 1, g->name);
            if (ls) {
                g->off = c.data_off[g->obj] + ls->off;
            } else {
                /* bss：按对象/声明顺序累积 */
                long cum = 0;
                bool found = false;
                for (int oi = 0; oi < c.n && !found; oi++)
                    for (ObjBss *b = c.objs[oi]->bss; b; b = b->next) {
                        if (oi == g->obj && strcmp(b->name, g->name) == 0) {
                            g->off = c.bss_base + cum;
                            found = true;
                            break;
                        }
                        cum += b->size;
                    }
                if (!found) {
                    set_err(err, "symbol %s: missing definition", g->name);
                    goto done;
                }
            }
        }
    }

    /* 重定位输出：全部 text（crt0 在前）→ 全部 data → halt，与布局一致。
     * crt0 文本后紧跟 jmp halt：call main 的返回地址 = call 末尾 + 1，
     * main 返回时落在 jmp halt 上直接终止（M1 同款安全网，避免重入 main）。 */
    Buf buf = {0};
    for (int oi = 0; oi < c.n; oi++) {
        for (int i = 0; i < c.objs[oi]->n_text; i++)
            emit_line(&c, oi, 0, c.objs[oi]->text_lines[i], &buf);
        if (oi == 0)
            buf_printf(&buf, "    jmp halt\n");
    }
    for (int oi = 0; oi < c.n; oi++)
        for (int i = 0; i < c.objs[oi]->n_data; i++)
            emit_line(&c, oi, 1, c.objs[oi]->data_lines[i], &buf);
    if (c.failed) {
        free(buf.p);
        goto done;
    }
    buf_printf(&buf, "\nhalt:\n    jmp halt\n");

    if (out_asm)
        fwrite(buf.p, 1, buf.len, out_asm);
    if (out_bin) {
        size_t cap = buf.len * 2 + 64;
        uint8_t *bin = (uint8_t *)malloc(cap);
        AsmError aerr;
        int n = asm_assemble(buf.p, bin, cap, &aerr);
        if (n < 0) {
            set_err(err, "assemble failed (line %d): %s", aerr.line, aerr.msg);
            free(bin);
            free(buf.p);
            goto done;
        }
        fwrite(bin, 1, (size_t)n, out_bin);
        free(bin);
    }
    free(buf.p);
    rc = true;

done:
    obj_free(c.objs[0]);
    free(c.objs);
    free(c.text_off);
    free(c.data_off);
    for (LocalSym *ls = c.locals; ls; ) {
        LocalSym *nx = ls->next;
        free(ls->name);
        free(ls);
        ls = nx;
    }
    for (GSymbol *g = c.globals; g; ) {
        GSymbol *nx = g->next;
        free(g->name);
        free(g);
        g = nx;
    }
    return rc;
}
