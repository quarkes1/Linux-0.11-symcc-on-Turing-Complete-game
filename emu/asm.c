/* emu/asm.c — SymphonyPlus 迷你汇编器
 *
 * 两遍扫描：第一遍解析各行、记录 label 定义（label 值 = 字节地址）并累计偏移；
 * 第二遍重新解析、把 label 引用解析为地址并编码输出。
 * 寄存器名（zr/r1..r13/sp/flags）为保留字：不可用作 label，imm 位置出现
 * 寄存器名视为非法（保证同名多变体按操作数类型匹配时无歧义）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "emu/asm.h"
#include "emu/isa.h"

#define MAX_LABELS  4096
#define MAX_LINE    4096

typedef struct {
    char name[64];
    uint32_t off;
} Label;

static Label labels[MAX_LABELS];
static int nlabels;

static void set_err(AsmError *err, int line, const char *fmt, ...) {
    va_list ap;
    err->line = line;
    va_start(ap, fmt);
    vsnprintf(err->msg, sizeof err->msg, fmt, ap);
    va_end(ap);
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

/* 复制下一行到 buf（去掉注释；字符串内的 ; 与 // 不截断）。
 * 返回续读位置；EOF 返回 NULL。lineno 累计物理行号。 */
static const char *next_line(const char *p, int *lineno, char *buf, size_t bufsz) {
    const char *start = p;
    size_t i = 0;
    int in_str = 0;
    while (*p) {
        char c = *p;
        if (c == '\n') { p++; break; }
        if (c == '\r') { p++; continue; }
        if (c == '"') in_str = !in_str;
        /* 行注释：吞掉剩余整行（含换行），否则注释文本会变成"下一行" */
        if (!in_str && (c == ';' || (c == '/' && p[1] == '/'))) {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            break;
        }
        if (i + 1 < bufsz) buf[i++] = c;
        p++;
    }
    if (p == start) return NULL;
    (*lineno)++;
    buf[i] = 0;
    return p;
}

/* 数字：0x… 十六进制或十进制；拒绝负数。返回 0 成功。 */
static int parse_num(const char *s, uint64_t *val) {
    char *end;
    if (*s == '-') return -1;
    *val = strtoull(s, &end, 0);
    return (*end == 0 && end != s) ? 0 : -1;
}

static int is_ident(const char *s) {
    if (!(*s == '_' || isalpha((unsigned char)*s))) return 0;
    for (s++; *s; s++)
        if (!(*s == '_' || isalnum((unsigned char)*s))) return 0;
    return 1;
}

/* 记录 label 定义。返回 0 成功。 */
static int add_label(const char *name, uint32_t off, int lineno, AsmError *err) {
    if (!is_ident(name)) {
        set_err(err, lineno, "bad label name '%s'", name);
        return -1;
    }
    if (isa_reg_index(name) >= 0) {
        set_err(err, lineno, "label '%s' shadows a register name", name);
        return -1;
    }
    for (int i = 0; i < nlabels; i++)
        if (strcmp(labels[i].name, name) == 0) {
            /* 重复 label：保留首个（first-wins），与游戏汇编器容忍行为一致。
             * 链接器桩化后重复的静态函数名已无 label 引用（调用走池绝对地址），
             * 如内核的 init（init/main.c 与 chr_drv 各有一个）。 */
            fprintf(stderr, "asm: warning: duplicate label '%s' (line %d, keeping first)\n",
                    name, lineno);
            return 0;
        }
    if (nlabels >= MAX_LABELS) {
        set_err(err, lineno, "too many labels (max %d)", MAX_LABELS);
        return -1;
    }
    snprintf(labels[nlabels].name, sizeof labels[nlabels].name, "%s", name);
    labels[nlabels].off = off;
    nlabels++;
    return 0;
}

static uint32_t label_off(const char *name) {
    for (int i = 0; i < nlabels; i++)
        if (strcmp(labels[i].name, name) == 0)
            return labels[i].off;
    return UINT32_MAX;
}

/* 解析一个 IMM 操作数：字符字面量 / 数字 / label（resolve=1 时解析）。
 * 返回 0 成功；-1 = 变体不匹配（继续尝试其他变体）；-2 = 硬错误。 */
static int parse_imm(const char *op, uint32_t *val, int resolve, int lineno, AsmError *err) {
    if (op[0] == '\'' && op[1] && op[2] == '\'' && op[3] == 0) {
        *val = (uint8_t)op[1];
        return 0;
    }
    if (isa_reg_index(op) >= 0)
        return -1;                      /* 寄存器名不能当 imm（保留字） */
    {
        uint64_t v;
        if (parse_num(op, &v) == 0) {
            *val = (uint32_t)v;
            return 0;
        }
    }
    if (!is_ident(op))
        return -1;                      /* 不是数字/字符/label → 变体不匹配 */
    if (!resolve)
        return 0;                       /* 第一遍：label 引用暂不解析 */
    {
        uint32_t off = label_off(op);
        if (off == UINT32_MAX) {
            set_err(err, lineno, "undefined label '%s'", op);
            return -2;
        }
        *val = off;
        return 0;
    }
}

/* 解析一行指令操作数。成功填 vals 并返回 0；变体不匹配返回 -1。
 * resolve=1 时解析 label 与 16 位越界检查。 */
static int parse_operands(const Insn *insn, char *ops_text, uint32_t *vals,
                          int resolve, int lineno, AsmError *err) {
    char *tok = ops_text;
    int vi = 0;
    int w, o;

    for (w = 0; w < insn->nwords; w++) {
        for (o = 0; o < MAX_OPS; o++) {
            const OpField *f = &insn->ops[w][o];
            char *t, *comma;
            size_t len;
            int ri;

            if (f->kind == OP_NONE) continue;

            t = tok;
            comma = strchr(t, ',');
            if (comma) { *comma = 0; tok = comma + 1; }
            else { tok = t + strlen(t); }    /* 末操作数：tok 越过它，供"过多"检查 */
            t = trim(t);
            if (*t == 0) return -1;          /* 操作数不足 */

            len = strlen(t);
            if (len >= 2 && t[0] == '[' && t[len - 1] == ']') {
                t[len - 1] = 0;
                t++;                          /* 去 [ ]（load/store 寻址） */
            }

            if (f->kind == OP_REG) {
                ri = isa_reg_index(t);
                if (ri < 0) return -1;        /* 期待寄存器，不是 → 变体不匹配 */
                vals[vi] = (uint32_t)ri;
            } else { /* OP_IMM */
                if (parse_imm(t, &vals[vi], resolve, lineno, err) != 0) {
                    if (err->line) return -2; /* 硬错误（未定义 label 等） */
                    return -1;
                }
                if (resolve && vals[vi] > 0xFFFF) {
                    set_err(err, lineno, "immediate/label too large: 0x%x (>0xFFFF)", vals[vi]);
                    return -2;
                }
            }
            vi++;
        }
    }
    if (*trim(tok)) return -1;               /* 操作数过多 → 变体不匹配 */
    return vi == insn->opcount ? 0 : -1;
}

/* 指令行：mnem 为助记符，ops 为剩余操作数文本。resolve 语义同上。
 * pass2 时编码到 out[off]，返回字节数。 */
static int emit_insn(const char *mnem, char *ops, int resolve, uint32_t off,
                     uint8_t *out, size_t cap, int lineno, AsmError *err) {
    for (int i = 0; i < isa_table_len; i++) {
        const Insn *insn = &isa_table[i];
        uint32_t vals[MAX_OPS];
        int n;
        if (strcmp(insn->name, mnem) != 0) continue;
        /* parse_operands 会就地改写（逗号→NUL），故每变体用副本 */
        char ops_copy[MAX_LINE];
        strcpy(ops_copy, ops);
        if (parse_operands(insn, ops_copy, vals, resolve, lineno, err) != 0) {
            if (err->line) return -1;
            continue;
        }
        n = insn->nwords * 4;
        if (resolve) {
            if (off + (uint32_t)n > cap) {
                set_err(err, lineno, "output too large", (const char *)NULL);
                return -1;
            }
            return isa_encode(insn, vals, out + off);
        }
        return n;
    }
    set_err(err, lineno, "unknown instruction '%s' or bad operands", mnem);
    return -1;
}

/* 一行数据：U8/U16/U32/U64 大端发射。非数据行返回 -1 且不置错。 */
static int emit_data(const char *rest, int resolve, uint32_t off,
                     uint8_t *out, size_t cap, int lineno, AsmError *err) {
    static const struct { const char *name; int width; } datas[] = {
        { "U8", 1 }, { "U16", 2 }, { "U32", 4 }, { "U64", 8 },
    };
    int width = 0;
    uint64_t v;
    uint8_t tmp[8];
    const char *p = rest;

    for (int i = 0; i < 4; i++) {
        size_t l = strlen(datas[i].name);
        if (strncmp(rest, datas[i].name, l) == 0 &&
            (rest[l] == ' ' || rest[l] == '\t')) {
            width = datas[i].width;
            p = skip_ws(rest + l);
            break;
        }
    }
    if (width == 0) return -1;              /* 非数据行，不置错 */

    if (parse_num(p, &v) != 0) {
        set_err(err, lineno, "bad number in '%s'", rest);
        return -1;
    }
    if (width < 8 && v >= (1ULL << (width * 8))) {
        set_err(err, lineno, "value too large for U%d", width);
        return -1;
    }
    for (int i = 0; i < width; i++)
        tmp[i] = (uint8_t)(v >> (8 * (width - 1 - i)));   /* 大端 */

    if (resolve) {
        if (off + (uint32_t)width > cap) {
            set_err(err, lineno, "output too large", (const char *)NULL);
            return -1;
        }
        memcpy(out + off, tmp, (size_t)width);
    }
    return width;
}

/* 字符串字面量：原样字节（不补 NUL——NUL 由编译器负责）。 */
static int emit_string(const char *rest, int resolve, uint32_t off,
                       uint8_t *out, size_t cap, int lineno, AsmError *err) {
    const char *p = rest + 1;               /* 跳过开引号 */
    size_t len = 0;

    while (*p && *p != '"') p++, len++;
    if (*p != '"') {
        set_err(err, lineno, "unterminated string", (const char *)NULL);
        return -1;
    }
    if (resolve) {
        if (off + (uint32_t)len > cap) {
            set_err(err, lineno, "output too large", (const char *)NULL);
            return -1;
        }
        memcpy(out + off, rest + 1, len);
    }
    return (int)len;
}

/* @0x… 对齐填充。返回填充字节数（含 0）。 */
static int emit_pad(const char *rest, int resolve, uint32_t off,
                    uint8_t *out, size_t cap, int lineno, AsmError *err) {
    uint64_t target;
    if (parse_num(rest + 1, &target) != 0) {
        set_err(err, lineno, "bad pad address '%s'", rest);
        return -1;
    }
    if (target < off) {
        set_err(err, lineno, "pad target 0x%llx before current offset", (unsigned long long)target);
        return -1;
    }
    if (target - off > cap) {
        set_err(err, lineno, "output too large", (const char *)NULL);
        return -1;
    }
    if (resolve)
        memset(out + off, 0, (size_t)(target - off));
    return (int)(target - off);
}

/* 处理一行（可能含 label 前缀 + 后续内容）。
 * pass2 时编码到 out。返回该行消耗的字节数；出错返回 -1。 */
static int process_line(char *line, int resolve, uint32_t off,
                        uint8_t *out, size_t cap, int lineno, AsmError *err) {
    char *p = trim(line);
    char *colon;
    int n;

    if (*p == 0) return 0;                  /* 空行/纯注释 */

    /* label 定义（name: 且冒号后是行尾或空白；前缀必须是合法标识符）。
     * 注意：截断的是冒号本身（*colon），而非冒号后的字符。 */
    colon = strchr(p, ':');
    if (colon) {
        char saved = *colon;
        *colon = 0;
        if (is_ident(trim(p))) {
            char *rest;
            /* pass 1 记录 label；pass 2 跳过（label 表已由 pass 1 建好） */
            if (!resolve && add_label(trim(p), off, lineno, err) != 0)
                return -1;
            *colon = saved;                 /* 恢复冒号，继续解析行尾 */
            rest = trim(colon + 1);
            if (*rest == 0) return 0;       /* 纯 label 行 */
            p = rest;                       /* label + 指令/数据同行 */
        } else {
            *colon = saved;
        }
    }

    if (*p == '@')
        return emit_pad(p, resolve, off, out, cap, lineno, err);
    if (*p == '"')
        return emit_string(p, resolve, off, out, cap, lineno, err);
    if (isalpha((unsigned char)*p)) {
        n = emit_data(p, resolve, off, out, cap, lineno, err);
        if (n >= 0) return n;
        if (err->line) return -1;           /* 是数据行但内容错误 */
        /* 非数据 → 指令行 */
        char *sp = p;
        while (isalnum((unsigned char)*sp) || *sp == '_') sp++;
        if (sp == p) {
            set_err(err, lineno, "bad line '%s'", p);
            return -1;
        }
        char mnem[64];
        size_t mlen = (size_t)(sp - p);
        if (mlen >= sizeof mnem) mlen = sizeof mnem - 1;
        memcpy(mnem, p, mlen);
        mnem[mlen] = 0;
        sp = trim(sp);
        return emit_insn(mnem, sp, resolve, off, out, cap, lineno, err);
    }
    set_err(err, lineno, "bad line '%s'", p);
    return -1;
}

int asm_assemble(const char *text, uint8_t *out, size_t cap, AsmError *err) {
    const char *p;
    int lineno = 0;
    uint32_t off = 0;
    char line[MAX_LINE];

    err->line = 0;
    err->msg[0] = 0;
    nlabels = 0;

    /* 第一遍：label 定义 + 偏移累计 */
    p = text;
    while ((p = next_line(p, &lineno, line, sizeof line))) {
        int n = process_line(line, 0, off, NULL, cap, lineno, err);
        if (n < 0) return -1;
        off += (uint32_t)n;
    }
    if (off > cap) {
        set_err(err, 0, "output too large (%u bytes > %u)", off, (unsigned)cap);
        return -1;
    }

    /* 第二遍：编码 */
    p = text;
    lineno = 0;
    off = 0;
    while ((p = next_line(p, &lineno, line, sizeof line))) {
        int n = process_line(line, 1, off, out, cap, lineno, err);
        if (n < 0) return -1;
        off += (uint32_t)n;
    }
    return (int)off;
}
