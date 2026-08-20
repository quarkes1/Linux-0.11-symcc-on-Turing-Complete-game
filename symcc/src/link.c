/* symcc/src/link.c — symld 链接器核心
 *
 * 输入：N 个 .sym 可重定位对象 + crt0 模板（obj[0]，仅 text 行）。
 * 布局（大程序支持——跳转立即数仅 16 位绝对地址）：
 *   低区（J16 可寻址 < 64KB）：
 *     0:     crt0 text
 *     其后:  jmp halt（main 返回安全网）+ halt:（自循环）
 *     :      文字池（4B × n_pool：条件目标 + call + jmp 目标地址）
 *     :      跳板区（8B × n_tramp：load_32 r13,[池i]; jmp r13）
 *     :      bss（清零由 crt0；地址 < 64KB → 数据引用 D16 原样）
 *   text: 所有对象代码（>64KB 允许）。条件跳转 → je 跳板（跳板在
 *         低区，imm16 可编码；跳板经 r13 全 32 位跳转）；call →
 *         24B 桩（counter/add24/sub/store/load_32 r13,[池]/jmp r13）；
 *         jmp 符号目标 → 8B 桩。动态调用 jmp r1 原样。
 *   data: 所有对象数据（>64KB 允许；访问须编译期拆 D32——@hi:@lo）。
 * 引用解析（Task 5 引用形态）：
 *   @name          D16：全局符号 → 绝对地址；否则当前对象段内标签
 *                  → 段基址 + 段内偏移（>0xFFFF → 提示 --d32）
 *   @hi:name       D32 高半字（无范围检查）
 *   @lo:name       D32 低半字
 * 输出：绝对地址 asm（数据引用全数字；全局函数标签行保留供阅读；
 * L/s/static 标签解析为数字、定义行丢弃）。
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

/* 条件跳转目标（对象内 label 名）→ 跳板。跳板数 = 唯一目标数；
 * 多条件跳转共享同一跳板（3680 跳 → ~3145 唯一目标）。 */
typedef struct Tramp {
    struct Tramp *next;
    int obj;
    char *name;         /* 条件跳转目标 label */
    long addr;          /* 跳板绝对地址（布局后填） */
    long target;        /* 目标绝对地址（text 布局后填） */
    int pool_idx;       /* 池条目序号（跳板 i 经池条目 i 读 target） */
} Tramp;

/* call/jmp 符号目标 → 池条目（每个唯一目标一条，共享；值 = 目标地址）。
 * 池条目布局顺序：n_tramp 条（跳板 i 用条目 i）→ n_slots 条（唯一目标）。 */
typedef struct PoolRef {
    struct PoolRef *next;
    int obj;
    char *name;         /* 目标符号名（全局函数或段内标签） */
    int rslot;          /* 槽位序号（n_tramp 起相对；layout 后 = n_tramp + rslot） */
} PoolRef;

/* call/jmp 目标去重表（跨对象按名共享槽位） */
typedef struct PoolSlot {
    struct PoolSlot *next;
    int obj;
    char *name;         /* 目标符号名 */
    int rslot;          /* 槽位序号（0..n_slots-1） */
    long target;        /* 目标绝对地址（text 布局后填） */
} PoolSlot;

typedef struct Ctx {
    Obj **objs;
    int n;              /* 对象总数（crt0 = 0） */
    long text_size, data_base, data_size, bss_base, bss_total;
    long *text_off;     /* 每个对象的 text 绝对基址（对象内偏移 + 此基址） */
    long *data_off;     /* 每个对象的 data 绝对基址 */
    /* 低区布局（< 64KB，J16 可寻址） */
    long halt_base;     /* halt: 绝对地址（crt0 后 4 字节 jmp halt 处） */
    long pool_base;     /* 文字池基址 */
    long tramp_base;    /* 跳板区基址 */
    Tramp *tramps;      /* 条件跳转目标表（布局前收集） */
    int n_tramp;
    PoolRef *pool_refs; /* call/jmp 使用点表（布局前收集，反链序） */
    PoolSlot *pool_slots; /* 目标去重表 */
    int n_pool;         /* 池条目总数 = n_tramp + n_slots */
    int n_slots;        /* 唯一 call/jmp 目标数 */
    int n_call, n_jmp;  /* 使用点数（行扫描序解析/寻址） */
    int *ci_idx, *ji_idx; /* 行扫描序 → 槽位序号（emit 寻址用） */
    LocalSym *locals;
    GSymbol *globals;
    LinkError *err;
    bool failed;
    int call_emitted, jmp_emitted;  /* emit 时的池寻址计数 */
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

/* 跳转/调用行（"<op> <target>"，容忍前导空白）的助记符与目标操作数 */
static void jump_parts(const char *line, char op[8], char tgt[512]) {
    const char *p = line;
    while (*p == ' ')
        p++;
    int i = 0;
    while (*p && *p != ' ' && i < 7)
        op[i++] = *p++;
    op[i] = 0;
    while (*p == ' ')
        p++;
    i = 0;
    while (*p && *p != ';' && *p != ' ' && i < 511)
        tgt[i++] = *p++;
    tgt[i] = 0;
}

/* 桩化后行大小：条件跳转 4B（je 数字）不变；call 6 词 24B；
 * jmp 符号目标 2 词 8B；其余原样 */
static long line_size_after(Ctx *c, int obj, int i) {
    const char *l = c->objs[obj]->text_lines[i];
    char op[8], tgt[512];
    jump_parts(l, op, tgt);
    if (strcmp(op, "call") == 0 && is_sym_ident(tgt))
        return 24;
    if (strcmp(op, "jmp") == 0 && is_sym_ident(tgt) && !is_reg_name(tgt))
        return 8;
    return obj_line_size(l);
}

/* 布局前收集跳转目标（依赖标签/符号表已建）：
 * 条件目标 → tramp（对象内按名去重）；call/jmp 符号目标 → 池槽位
 * （跨对象按名去重共享；每个使用点记槽位序号 rslot）。 */
static void collect_jumps(Ctx *c) {
    for (int oi = 0; oi < c->n; oi++) {
        for (int i = 0; i < c->objs[oi]->n_text; i++) {
            const char *l = c->objs[oi]->text_lines[i];
            char op[8], tgt[512];
            jump_parts(l, op, tgt);
            if (is_cond_jump(op)) {
                Tramp *dup = NULL;
                for (Tramp *t = c->tramps; t; t = t->next)
                    if (t->obj == oi && strcmp(t->name, tgt) == 0)
                        dup = t;
                if (dup)
                    continue;
                Tramp *t = (Tramp *)calloc(1, sizeof *t);
                t->obj = oi;
                t->name = strdup(tgt);
                t->next = c->tramps;
                c->tramps = t;
                c->n_tramp++;
            } else if ((strcmp(op, "call") == 0 || strcmp(op, "jmp") == 0) &&
                       is_sym_ident(tgt) && !is_reg_name(tgt)) {
                PoolSlot *slot = NULL;
                for (PoolSlot *s = c->pool_slots; s; s = s->next)
                    if (s->obj == oi && strcmp(s->name, tgt) == 0) {
                        slot = s;
                        break;
                    }
                if (!slot) {
                    slot = (PoolSlot *)calloc(1, sizeof *slot);
                    slot->obj = oi;
                    slot->name = strdup(tgt);
                    slot->rslot = c->n_slots++;
                    slot->next = c->pool_slots;
                    c->pool_slots = slot;
                }
                PoolRef *p = (PoolRef *)calloc(1, sizeof *p);
                p->obj = oi;
                p->name = strdup(tgt);
                p->rslot = slot->rslot;
                p->next = c->pool_refs;
                c->pool_refs = p;
                if (strcmp(op, "call") == 0)
                    c->n_call++;
                else
                    c->n_jmp++;
            }
        }
    }
    c->n_pool = c->n_tramp + c->n_slots;
}

/* 使用点 (obj,name) → 槽位序号（行扫描序解析/寻址用） */
static int slot_rslot_of(Ctx *c, int obj, const char *name) {
    for (PoolSlot *s = c->pool_slots; s; s = s->next)
        if (s->obj == obj && strcmp(s->name, name) == 0)
            return s->rslot;
    return -1;
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

/* 跳转目标符号（全局函数或段内标签）→ 绝对地址。找不到 → 写错误 false */
static bool resolve_jtarget(Ctx *c, int obj, const char *name, long *out) {
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
    set_err(c->err, "undefined jump target: %s", name);
    return false;
}

/* 静态 bss 变量（.bss 行，无 label 行）：按对象/声明顺序累积定位。
 * bss 在低区（< 64KB）→ D16 引用合法。 */
static bool find_bss_sym(Ctx *c, int obj, const char *name, long *out) {
    long cum = 0;
    for (int oi = 0; oi < c->n; oi++)
        for (ObjBss *b = c->objs[oi]->bss; b; b = b->next) {
            if (oi == obj && strcmp(b->name, name) == 0) {
                *out = c->bss_base + cum;
                return true;
            }
            cum += b->size;
        }
    return false;
}

/* @name 解析：全局符号 → 绝对地址；否则当前对象段内标签 →
 * 对象段基址 + 段内偏移；再否则静态 bss 变量。未定义 → 写错误返回 false */
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
    if (find_bss_sym(c, obj, name, out))
        return true;
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
    /* 跳转/调用行桩化（J16 目标 > 64KB 不可编码——全部走低区池/跳板）：
     *   条件跳转  → je <跳板>（跳板在低区 < 64KB；跳板读池后 jmp r13）
     *   call      → 6 词桩：counter/add24/sub/store/load_32 r13,[池]/jmp r13
     *   jmp 符号  → 2 词桩：load_32 r13,[池]; jmp r13
     *   jmp 寄存器（动态调用）→ 原样（已是全 32 位） */
    char op[8], tgt[512];
    jump_parts(line, op, tgt);
    if (is_cond_jump(op)) {
        Tramp *t = NULL;
        for (Tramp *x = c->tramps; x; x = x->next)
            if (x->obj == obj && strcmp(x->name, tgt) == 0) {
                t = x;
                break;
            }
        if (!t) {
            set_err(c->err, "internal: missing tramp for %s", tgt);
            c->failed = true;
            return;
        }
        buf_printf(b, "    %s 0x%lx\n", op, t->addr);
        return;
    }
    if (strcmp(op, "call") == 0 && is_sym_ident(tgt)) {
        long pa = c->pool_base +
                  4L * (c->n_tramp + c->ci_idx[c->call_emitted++]);
        buf_printf(b, "    counter flags         ; call %s\n", tgt);
        buf_printf(b, "    add flags, flags, 24 ; 返回地址 = 桩首 + 24\n");
        buf_printf(b, "    sub sp, sp, 4\n");
        buf_printf(b, "    store_32 [sp], flags\n");
        buf_printf(b, "    load_32 r13, [0x%lx]\n", pa);
        buf_printf(b, "    jmp r13\n");
        return;
    }
    if (strcmp(op, "jmp") == 0 && is_sym_ident(tgt) && !is_reg_name(tgt)) {
        long pa = c->pool_base +
                  4L * (c->n_tramp + c->ji_idx[c->jmp_emitted++]);
        buf_printf(b, "    load_32 r13, [0x%lx]\n", pa);
        buf_printf(b, "    jmp r13\n");
        return;
    }
    /* 指令/数据行：词级重建 */
    const char *rest = line;
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
            } else if (strncmp(w, "@32:", 4) == 0) {
                /* 数据指针 reloc：完整 32 位地址（text/data 均 >64KB） */
                if (!resolve_dref(c, obj, w + 4, &v)) { c->failed = true; return; }
                const char *np = rest;
                while (*np == ' ')
                    np++;
                if (*np == '+') {
                    np++;
                    while (*np == ' ')
                        np++;
                    char *endp = NULL;
                    long add = strtol(np, &endp, 0);
                    if (endp != np && (*endp == 0 || *endp == ' ' || *endp == ';')) {
                        v += add;
                        rest = endp;
                    }
                }
                buf_printf(b, "0x%08lx", v);
            } else {
                if (!resolve_dref(c, obj, w + 1, &v)) { c->failed = true; return; }
                /* 加法数（数据行 `U32 @name + 0xK`，编译器地址常量
                 * 表达式 &g[0].m 的静态偏移）：折叠为单个数字 */
                const char *np = rest;
                while (*np == ' ')
                    np++;
                if (*np == '+') {
                    np++;
                    while (*np == ' ')
                        np++;
                    char *endp = NULL;
                    long add = strtol(np, &endp, 0);
                    if (endp != np && (*endp == 0 || *endp == ' ' || *endp == ';')) {
                        v += add;
                        rest = endp;
                    }
                }
                if (v > 0xFFFF) {
                    set_err(c->err, "reference @%s = 0x%lx out of 16-bit range (use --d32)",
                            w + 1, v);
                    c->failed = true;
                    return;
                }
                buf_printf(b, "0x%lx", v);
            }
        } else {
            buf_printf(b, "%s", w);
        }
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

    /* 布局（大程序支持，见文件头）：
     *   低区: crt0 → jmp halt(4) → halt:(4) → 文字池(4B×n_pool)
     *         → 跳板区(8B×n_tramp) → bss（清零由 crt0，须整体 < 64KB）
     *   text: 从 bss 结束起（>64KB 允许——跳转已桩化/跳板化）
     *   data: text 后（访问须编译期拆 D32）
     * 每对象记录段绝对基址（text_off/data_off）：重定位 = 基址 + 段内偏移。 */
    for (int oi = 0; oi < c.n; oi++)
        for (ObjBss *b = c.objs[oi]->bss; b; b = b->next)
            c.bss_total += b->size;
    collect_jumps(&c);      /* 统计池/跳板大小（只依赖对象行） */
    c.text_off = (long *)calloc((size_t)c.n, sizeof(long));
    c.data_off = (long *)calloc((size_t)c.n, sizeof(long));
    if (!c.text_off || !c.data_off) {
        set_err(err, "out of memory");
        goto done;
    }
    long crt0_size = 0;
    for (int i = 0; i < c.objs[0]->n_text; i++)
        crt0_size += line_size_after(&c, 0, i);
    c.halt_base = crt0_size + 4;                    /* 后随 jmp halt（4B） */
    c.pool_base = align4(c.halt_base + 4);          /* halt: 指令 4B */
    c.tramp_base = align4(c.pool_base + 4L * (c.n_tramp + c.n_slots));
    long low_end = c.tramp_base + 8L * c.n_tramp;   /* 跳板区结束 = bss 起点 */
    c.bss_base = align4(low_end);
    if (c.bss_base + c.bss_total > 0x10000) {
        set_err(err, "low region exceeds 64KB (J16 limit): crt0+pool+tramp+bss "
                     "= %ld bytes", c.bss_base + c.bss_total);
        goto done;
    }
    /* crt0 在低区 0x0（其段内标签按 0 基址解析）；对象文本从 bss 后起 */
    c.text_off[0] = 0;
    long toff = align4(c.bss_base + c.bss_total);
    for (int oi = 1; oi < c.n; oi++) {
        c.text_off[oi] = toff;
        for (int i = 0; i < c.objs[oi]->n_text; i++)
            toff += line_size_after(&c, oi, i);
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
            /* 与布局一致：桩化行（call 24B / jmp 8B）按桩后大小计 */
            off += line_size_after(&c, oi, i);
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
                strcmp(s->name, "__bss_end") == 0 ||
                strcmp(s->name, "end") == 0) {
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
    /* end = 内核映像末尾（Linux 0.11 链接脚本符号：buffer 池起点） */
    GSymbol *bend2 = (GSymbol *)calloc(1, sizeof *bend2);
    bend2->name = strdup("end");
    bend2->builtin = true;
    bend2->off = bend->off;
    bend2->next = c.globals;
    c.globals = bend2;

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

    /* 跳转目标解析（符号表已建、text 已布局）：
     * tramp 按链表序分配地址（pool_idx = 序，池条目 = 对应 target）；
     * call/jmp 池值按对象行扫描序收集（与 emit 的寻址序一致）。 */
    int tk = 0;
    for (Tramp *t = c.tramps; t; t = t->next) {
        if (!resolve_jtarget(&c, t->obj, t->name, &t->target))
            goto done;
        t->pool_idx = tk;
        t->addr = c.tramp_base + 8L * tk++;
    }
    /* 槽位值（唯一目标地址）+ 行扫描序 → 槽位索引（emit 寻址同序） */
    long *pt = (long *)calloc((size_t)(c.n_slots ? c.n_slots : 1),
                              sizeof(long));
    c.ci_idx = (int *)calloc((size_t)(c.n_call ? c.n_call : 1), sizeof(int));
    c.ji_idx = (int *)calloc((size_t)(c.n_jmp ? c.n_jmp : 1), sizeof(int));
    if (!pt || !c.ci_idx || !c.ji_idx) {
        set_err(err, "out of memory");
        free(pt);
        free(c.ci_idx);
        free(c.ji_idx);
        c.ci_idx = c.ji_idx = NULL;
        goto done;
    }
    {
        int ci = 0, ji = 0;
        for (int oi = 0; oi < c.n; oi++)
            for (int i = 0; i < c.objs[oi]->n_text; i++) {
                char op[8], tgt[512];
                jump_parts(c.objs[oi]->text_lines[i], op, tgt);
                if (strcmp(op, "call") == 0 && is_sym_ident(tgt)) {
                    int rs = slot_rslot_of(&c, oi, tgt);
                    c.ci_idx[ci++] = rs;
                    if (!resolve_jtarget(&c, oi, tgt, &pt[rs])) {
                        free(pt);
                        free(c.ci_idx);
                        free(c.ji_idx);
                        c.ci_idx = c.ji_idx = NULL;
                        goto done;
                    }
                } else if (strcmp(op, "jmp") == 0 && is_sym_ident(tgt) &&
                           !is_reg_name(tgt)) {
                    int rs = slot_rslot_of(&c, oi, tgt);
                    c.ji_idx[ji++] = rs;
                    if (!resolve_jtarget(&c, oi, tgt, &pt[rs])) {
                        free(pt);
                        free(c.ci_idx);
                        free(c.ji_idx);
                        c.ci_idx = c.ji_idx = NULL;
                        goto done;
                    }
                }
            }
    }

    /* 重定位输出（与布局一致）：crt0 → jmp halt → halt → 池 → 跳板
     * → 其余对象 text → data。crt0 后紧跟 jmp halt：main 返回安全网。 */
    Buf buf = {0};
    c.call_emitted = 0;
    c.jmp_emitted = 0;
    for (int i = 0; i < c.objs[0]->n_text; i++)
        emit_line(&c, 0, 0, c.objs[0]->text_lines[i], &buf);
    buf_printf(&buf, "    jmp halt\n");
    buf_printf(&buf, "halt:\n    jmp halt\n");
    buf_printf(&buf, "; jump target pool (%d entries)\n", c.n_pool);
    for (Tramp *t = c.tramps; t; t = t->next)
        buf_printf(&buf, "    U32 0x%08lx\n", t->target);
    for (int k = 0; k < c.n_slots; k++)
        buf_printf(&buf, "    U32 0x%08lx\n", pt[k]);
    buf_printf(&buf, "; trampolines (%d)\n", c.n_tramp);
    for (Tramp *t = c.tramps; t; t = t->next) {
        buf_printf(&buf, "    load_32 r13, [0x%lx]\n",
                   c.pool_base + 4L * t->pool_idx);
        buf_printf(&buf, "    jmp r13\n");
    }
    /* bss 占位：汇编器无 .bss 指令，输出等宽 U32 0（清零由 crt0）。
     * 缺此占位时汇编器布局比 text_off 计算少 bss_total 字节，
     * 全部符号地址错位，stub 化跳转会跳过函数入口的首词。 */
    for (long k = 0; k < (align4(c.bss_base + c.bss_total) - c.bss_base) / 4; k++)
        buf_printf(&buf, "    U32 0x00000000\n");
    for (int oi = 1; oi < c.n; oi++)
        for (int i = 0; i < c.objs[oi]->n_text; i++)
            emit_line(&c, oi, 0, c.objs[oi]->text_lines[i], &buf);
    for (int oi = 0; oi < c.n; oi++)
        for (int i = 0; i < c.objs[oi]->n_data; i++)
            emit_line(&c, oi, 1, c.objs[oi]->data_lines[i], &buf);
    free(pt);
    free(c.ci_idx);
    free(c.ji_idx);
    c.ci_idx = c.ji_idx = NULL;
    if (c.failed) {
        free(buf.p);
        goto done;
    }

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
    for (Tramp *t = c.tramps; t; ) {
        Tramp *nx = t->next;
        free(t->name);
        free(t);
        t = nx;
    }
    for (PoolRef *p = c.pool_refs; p; ) {
        PoolRef *nx = p->next;
        free(p->name);
        free(p);
        p = nx;
    }
    for (PoolSlot *s = c.pool_slots; s; ) {
        PoolSlot *nx = s->next;
        free(s->name);
        free(s);
        s = nx;
    }
    return rc;
}
