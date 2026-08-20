/* symcc/src/codegen.c — SymphonyPlus 汇编代码生成
 *
 * 约定：
 * - 表达式求值结果在 r1；r2 作二元运算第二操作数；r9 作地址暂存
 * - 二元运算：右操作数 → push 暂存 → 左操作数 → pop r2 → 运算（r1 op r2）
 * - 乘用 mul；int 全部按有符号比较（jl/jle/jg/jge）
 * - 局部变量：负偏移栈槽，相对帧基址 r10（函数入口 sp 处保存）
 * - 函数调用（全栈传参）：实参从右往左 push → call label → 调用方
 *   add sp,sp,4*nargs 清理。被调方序言 push r10（保存调用方帧指针）、
 *   mov r10,sp、sub sp,sp,frame；实参 k 在 [r10+8+4k]（push 后入口），
 *   拷入栈槽 [r10-4*(k+1)]；尾言 mov sp,r10; pop r10; ret
 * - 全局变量/字符串在数据段（label = 名字；16 位寻址，M1 程序 < 64KB）
 * - 立即数上限 0xFFFF（ISA 硬限制）：超出拆 16 位拼接，上限 0xFFFFFFFF
 * - 程序布局：crt0.asm（启动：配置屏幕 + 设栈 + call main）→ 函数 →
 *   数据段 → halt。main 是普通函数（crt0 负责栈初始化与入口）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"
#include "symcc/include/config.h"

#define XSTR(x) STR(x)
#define STR(x) #x

static FILE *out_asm;
static int label_cnt;

static void err_imm(Token *t, int64_t val) {
    fprintf(stderr, "constant too large for 16-bit immediate: %lld (%.*s)\n",
            (long long)val, t->len, t->loc);
    exit(1);
}

static const char *new_label(void) {
    char *buf = (char *)malloc(16);
    if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
    snprintf(buf, 16, "L%d", label_cnt++);
    return buf;
}

static void emit_label(const char *l) {
    fprintf(out_asm, "%s:\n", l);
}

/* 数字常量 → r1。
 * 立即数上限 16 位：>0xFFFF 拆高位/低位拼接（mov hi; lsl 16; or lo），
 * 上限 0xFFFFFFFF（M1 无 64 位） */
static void gen_num(int64_t val, Token *t) {
    if (val > 0xFFFFFFFFLL || val < 0)
        err_imm(t, val);
    int64_t hi = (val >> 16) & 0xFFFF;
    int64_t lo = val & 0xFFFF;
    if (hi) {
        fprintf(out_asm, "    mov r1, %lld        ; 常量高位\n", (long long)hi);
        fprintf(out_asm, "    lsl r1, r1, 16\n");
        if (lo)
            fprintf(out_asm, "    or r1, r1, %lld     ; 常量低位\n", (long long)lo);
    } else {
        fprintf(out_asm, "    mov r1, %lld        ; %lld\n", (long long)lo, (long long)val);
    }
}

/* 变量地址 → r9。局部变量相对帧基址 r10（函数入口 sp），
 * 因为 push/pop 会改变 sp，若相对 sp 则压栈后变量错位。 */
static void gen_addr(Node *n) {
    fprintf(out_asm, "    sub r9, r10, %d    ; addr of var\n", -n->offset);
}

/* 元素大小：char=1，int/指针=4（指针算术缩放用） */
static int size_of(Type *t) {
    return t->kind == TY_CHAR ? 1 : 4;
}

/* 按类型从 [r9] 加载到 r1：char 符号扩展（unsigned char 零扩展） */
static void gen_load(Type *t) {
    if (t->kind == TY_CHAR) {
        fprintf(out_asm, "    load_8 r1, [r9]     ; char\n");
        fprintf(out_asm, "    lsl r1, r1, 24\n");
        fprintf(out_asm, "    %s r1, r1, 24   ; %s扩展\n",
                t->is_unsigned ? "lsr" : "asr", t->is_unsigned ? "零" : "符号");
    } else {
        fprintf(out_asm, "    load_32 r1, [r9]    ; load\n");
    }
}

/* 按类型把 r1 存入 [r9]：char 用 store_8（天然截断低 8 位） */
static void gen_store(Type *t) {
    if (t->kind == TY_CHAR)
        fprintf(out_asm, "    store_8 [r9], r1    ; char\n");
    else
        fprintf(out_asm, "    store_32 [r9], r1   ; store\n");
}

/* 比较 materialize：r1 = (r1 op r2) ? 1 : 0；jmp_op = 真时跳转的助记符 */
static void gen_compare(const char *jmp_op, const char *opname) {
    const char *Ltrue = new_label();
    const char *Lend = new_label();
    fprintf(out_asm, "    cmp r1, r2\n");
    fprintf(out_asm, "    %s %s        ; %s\n", jmp_op, Ltrue, opname);
    fprintf(out_asm, "    mov r1, 0\n");
    fprintf(out_asm, "    jmp %s\n", Lend);
    emit_label(Ltrue);
    fprintf(out_asm, "    mov r1, 1\n");
    emit_label(Lend);
}

static void gen_expr(Node *n);

/* 短路逻辑：r1 = lhs && rhs（右操作数可能不执行） */
static void gen_logand(Node *n) {
    const char *Lfalse = new_label();
    const char *Lend = new_label();
    gen_expr(n->lhs);
    fprintf(out_asm, "    mov r2, 0\n");
    fprintf(out_asm, "    cmp r1, r2\n");
    fprintf(out_asm, "    je %s        ; && 短路\n", Lfalse);
    gen_expr(n->rhs);
    fprintf(out_asm, "    cmp r1, r2\n");
    fprintf(out_asm, "    je %s\n", Lfalse);
    fprintf(out_asm, "    mov r1, 1\n");
    fprintf(out_asm, "    jmp %s\n", Lend);
    emit_label(Lfalse);
    fprintf(out_asm, "    mov r1, 0\n");
    emit_label(Lend);
}

static void gen_logor(Node *n) {
    const char *Ltrue = new_label();
    const char *Lend = new_label();
    gen_expr(n->lhs);
    fprintf(out_asm, "    mov r2, 0\n");
    fprintf(out_asm, "    cmp r1, r2\n");
    fprintf(out_asm, "    jne %s        ; || 短路\n", Ltrue);
    gen_expr(n->rhs);
    fprintf(out_asm, "    cmp r1, r2\n");
    fprintf(out_asm, "    jne %s\n", Ltrue);
    fprintf(out_asm, "    mov r1, 0\n");
    fprintf(out_asm, "    jmp %s\n", Lend);
    emit_label(Ltrue);
    fprintf(out_asm, "    mov r1, 1\n");
    emit_label(Lend);
}

static void gen_expr(Node *n) {
    switch (n->kind) {
    case ND_NUM:
        gen_num(n->val, n->tok);
        return;
    case ND_VAR:
        gen_addr(n);
        gen_load(n->ty);
        return;
    case ND_GVAR:
        if (n->ty->kind == TY_CHAR)
            fprintf(out_asm, "    load_8 r1, [@%s]    ; global char\n", n->name);
        else
            fprintf(out_asm, "    load_32 r1, [@%s]   ; global\n", n->name);
        if (n->ty->kind == TY_CHAR) {
            fprintf(out_asm, "    lsl r1, r1, 24\n");
            fprintf(out_asm, "    %s r1, r1, 24   ; %s扩展\n",
                    n->ty->is_unsigned ? "lsr" : "asr",
                    n->ty->is_unsigned ? "零" : "符号");
        }
        return;
    case ND_STR:
        fprintf(out_asm, "    mov r1, @s%lld      ; string\n", (long long)n->val);
        return;
    case ND_ADDR:
        /* 地址 → r1：局部 = r10-|offset|；全局/字符串 = label 地址 */
        if (n->lhs->kind == ND_VAR)
            fprintf(out_asm, "    sub r1, r10, %d    ; addr of var\n", -n->lhs->offset);
        else if (n->lhs->kind == ND_GVAR)
            fprintf(out_asm, "    mov r1, @%s        ; addr of global\n", n->lhs->name);
        else
            fprintf(out_asm, "    mov r1, @s%lld     ; addr of string\n", (long long)n->lhs->val);
        return;
    case ND_DEREF:
        gen_expr(n->lhs);
        fprintf(out_asm, "    mov r9, r1          ; deref addr\n");
        gen_load(n->ty);
        return;
    case ND_ASSIGN:
        if (n->lhs->kind == ND_DEREF) {
            gen_expr(n->lhs->lhs);       /* 地址 */
            fprintf(out_asm, "    push r1            ; 暂存地址\n");
            gen_expr(n->rhs);
            fprintf(out_asm, "    pop r9             ; 取回地址\n");
            gen_store(n->lhs->ty);
        } else if (n->lhs->kind == ND_GVAR) {
            gen_expr(n->rhs);
            if (n->lhs->ty->kind == TY_CHAR)
                fprintf(out_asm, "    store_8 [@%s], r1  ; = global char\n", n->lhs->name);
            else
                fprintf(out_asm, "    store_32 [@%s], r1  ; = global\n", n->lhs->name);
        } else {
            gen_expr(n->rhs);
            gen_addr(n->lhs);
            gen_store(n->lhs->ty);
        }
        return;
    case ND_CALL: {
        /* 实参从右往左求值并 push（全栈传参）→ call → 清理 */
        Node *args[64];
        int na = 0;
        for (Node *a = n->rhs; a; a = a->next)
            args[na++] = a;
        for (int k = na - 1; k >= 0; k--) {
            gen_expr(args[k]);
            fprintf(out_asm, "    push r1            ; arg %d\n", k);
        }
        fprintf(out_asm, "    call %s\n", n->name);
        if (na > 0)
            fprintf(out_asm, "    add sp, sp, %d      ; 清理实参\n", 4 * na);
        return;
    }
    case ND_NEG:
        gen_expr(n->lhs);
        fprintf(out_asm, "    neg r1, r1         ; unary -\n");
        return;
    case ND_NOT:
        /* !x == (x == 0) */
        gen_expr(n->lhs);
        fprintf(out_asm, "    mov r2, 0\n");
        gen_compare("je", "!");
        return;
    case ND_LOGAND:
        gen_logand(n);
        return;
    case ND_LOGOR:
        gen_logor(n);
        return;
    case ND_ADD:
    case ND_SUB:
        /* 指针算术：p+n → n×元素大小；p-q（两个指针）→ 差÷元素大小 */
        {
            bool ptr_l = n->lhs->ty->kind == TY_PTR;
            bool ptr_r = n->rhs->ty->kind == TY_PTR;
            gen_expr(n->rhs);
            if (ptr_l && !ptr_r && size_of(n->lhs->ty->base) == 4)
                fprintf(out_asm, "    mul r1, r1, 4      ; ptr arith: n*4\n");
            fprintf(out_asm, "    push r1            ; 暂存右操作数\n");
            gen_expr(n->lhs);
            fprintf(out_asm, "    pop r2             ; 取回右操作数\n");
            if (n->kind == ND_ADD) {
                fprintf(out_asm, "    add r1, r1, r2     ; +\n");
            } else {
                fprintf(out_asm, "    sub r1, r1, r2     ; -\n");
                if (ptr_l && ptr_r && size_of(n->lhs->ty->base) == 4)
                    fprintf(out_asm, "    asr r1, r1, 2     ; ptr diff /4\n");
            }
            return;
        }
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_GT:
    case ND_GE:
        /* 右操作数 → push → 左操作数 → pop r2 → r1 op r2 */
        gen_expr(n->rhs);
        fprintf(out_asm, "    push r1            ; 暂存右操作数\n");
        gen_expr(n->lhs);
        fprintf(out_asm, "    pop r2             ; 取回右操作数\n");
        switch (n->kind) {
        case ND_MUL: fprintf(out_asm, "    mul r1, r1, r2     ; *\n"); return;
        case ND_DIV:
        case ND_MOD:
            if (n->ty->is_unsigned) {
                /* 硬件 div/mod 即无符号：直接指令 */
                fprintf(out_asm, "    %s r1, r1, r2     ; %s（无符号）\n",
                        n->kind == ND_DIV ? "div" : "mod",
                        n->kind == ND_DIV ? "/" : "%");
            } else {
                /* 有符号：调用运行时（全栈传参：arg0=左、arg1=右） */
                fprintf(out_asm, "    push r2            ; arg1 = 右操作数\n");
                fprintf(out_asm, "    push r1            ; arg0 = 左操作数\n");
                fprintf(out_asm, "    call __%s\n",
                        n->kind == ND_DIV ? "divsi3" : "modsi3");
                fprintf(out_asm, "    add sp, sp, 8      ; 清理实参\n");
            }
            return;
        case ND_EQ:  gen_compare("je", "=="); return;
        case ND_NE:  gen_compare("jne", "!="); return;
        case ND_LT:  gen_compare("jl", "<"); return;   /* 有符号 */
        case ND_LE:  gen_compare("jle", "<="); return;
        case ND_GT:  gen_compare("jg", ">"); return;
        case ND_GE:  gen_compare("jge", ">="); return;
        }
        return;
    default:
        fprintf(stderr, "codegen: unhandled node kind %d\n", n->kind);
        exit(1);
    }
}

/* 条件：r1 = 0/1 时跳转 op_label（等于 0 跳 je，非 0 跳 jne） */
static void gen_branch_zero(Node *cond, const char *label, bool jump_if_zero) {
    gen_expr(cond);
    fprintf(out_asm, "    mov r2, 0\n");
    fprintf(out_asm, "    cmp r1, r2\n");
    fprintf(out_asm, "    %s %s\n", jump_if_zero ? "je" : "jne", label);
}

static void gen_stmt(Node *n);

/* 语句链表：块 { ... } 产生链表，必须全部生成（曾只生成头语句，
 * 导致 while/if 体内第二条语句被丢弃 → i++ 缺失 → 死循环） */
static void gen_stmts(Node *n) {
    for (; n; n = n->next)
        gen_stmt(n);
}

static void gen_stmt(Node *n) {
    switch (n->kind) {
    case ND_RETURN:
        gen_expr(n->lhs);
        fprintf(out_asm, "    ; return\n");
        fprintf(out_asm, "    mov sp, r10\n");
        fprintf(out_asm, "    pop r10            ; 恢复调用方帧指针\n");
        fprintf(out_asm, "    ret\n");
        return;
    case ND_IF: {
        const char *Lelse = new_label();
        const char *Lend = new_label();
        gen_branch_zero(n->lhs, Lelse, true);
        gen_stmts(n->rhs);
        if (n->els) {
            fprintf(out_asm, "    jmp %s\n", Lend);
            emit_label(Lelse);
            gen_stmts(n->els);
            emit_label(Lend);
        } else {
            emit_label(Lelse);
        }
        return;
    }
    case ND_WHILE: {
        const char *Lbegin = new_label();
        const char *Lend = new_label();
        emit_label(Lbegin);
        gen_branch_zero(n->lhs, Lend, true);
        gen_stmts(n->rhs);
        fprintf(out_asm, "    jmp %s\n", Lbegin);
        emit_label(Lend);
        return;
    }
    case ND_FOR: {
        const char *Lbegin = new_label();
        const char *Lend = new_label();
        if (n->lhs)
            gen_expr(n->lhs);
        emit_label(Lbegin);
        if (n->rhs)
            gen_branch_zero(n->rhs, Lend, true);
        gen_stmts(n->body);
        if (n->els)
            gen_expr(n->els);
        fprintf(out_asm, "    jmp %s\n", Lbegin);
        emit_label(Lend);
        return;
    }
    case ND_VAR:
        /* 无初始化的声明（占位节点）：无操作 */
        return;
    default:
        gen_expr(n);
        return;
    }
}

/* 函数尾言（体末尾无 return 时的兜底；main 也是普通函数，
 * 返回后回到 crt0 的 halt） */
static void gen_epilogue(void) {
    fprintf(out_asm, "    mov sp, r10\n");
    fprintf(out_asm, "    pop r10            ; 恢复调用方帧指针\n");
    fprintf(out_asm, "    ret\n");
}

/* 函数序言 + 体 + 尾言 */
static void gen_func(Func *f) {
    fprintf(out_asm, "\n%s:\n", f->name);
    fprintf(out_asm, "    push r10             ; 保存调用方帧指针\n");
    fprintf(out_asm, "    mov r10, sp          ; 帧基址\n");
    if (f->frame_size)
        fprintf(out_asm, "    sub sp, sp, %d        ; 帧：局部变量 %d 字节\n",
                f->frame_size, f->frame_size);

    /* 参数拷入栈槽：实参 k 在 [r10+8+4k]（序言 push r10 后入口 sp = r10，
     * 实参原本在入口 sp+4+4k）；栈槽 = r10-4*(k+1)（参数先于局部分配） */
    for (int k = 0; k < f->nargs; k++) {
        fprintf(out_asm, "    add r9, r10, %d        ; arg %d addr\n", 8 + 4 * k, k);
        fprintf(out_asm, "    load_32 r1, [r9]\n");
        fprintf(out_asm, "    sub r9, r10, %d        ; arg %d slot\n", 4 * (k + 1), k);
        fprintf(out_asm, "    store_32 [r9], r1\n");
    }

    gen_stmts(f->body);
    gen_epilogue();
}

/* 数据段：全局变量（label = 名字）+ 字符串（label = s%d） */
static void emit_data(Program *prog) {
    for (Global *g = prog->globals; g; g = g->next) {
        fprintf(out_asm, "\n%s:\n", g->name);
        fprintf(out_asm, "    U32 %lld\n", (long long)g->init_val);
    }
    int idx = 0;
    for (Token *t = prog->strs; t; t = t->next) {
        fprintf(out_asm, "\ns%d:\n", idx++);
        /* 含 NUL 或引号的字符串不能用 "..." 原样发射（汇编器按引号/字节扫），
         * 退回逐字节 U8；其余用 "..." + 结尾 U8 0 */
        if (memchr(t->str, 0, (size_t)t->str_len) ||
            memchr(t->str, '"', (size_t)t->str_len)) {
            for (int i = 0; i < t->str_len; i++)
                fprintf(out_asm, "    U8 %d\n", (unsigned char)t->str[i]);
        } else {
            fprintf(out_asm, "    \"%.*s\"\n", t->str_len, t->str);
        }
        fprintf(out_asm, "    U8 0\n");
    }
}

/* 输出头：拼接 runtime/crt0.asm（FRAMEBUF_BASE 占位符 → config.h 值）。
 * crt0 在地址 0 执行：配置屏幕（ASCII 8 + 帧缓冲基址）、设栈、call main。 */
static void emit_crt0(void) {
    static char crt0[8192];
    FILE *fp = fopen("runtime/crt0.asm", "rb");
    if (!fp) {
        fprintf(stderr, "codegen: cannot open runtime/crt0.asm (run from repo root?)\n");
        exit(1);
    }
    size_t n = fread(crt0, 1, sizeof crt0 - 1, fp);
    fclose(fp);
    crt0[n] = 0;

    const char *val = XSTR(FRAMEBUF_BASE);
    const char *mark = "FRAMEBUF_BASE";
    const char *rest = crt0;
    for (const char *hit = strstr(rest, mark); hit; hit = strstr(rest, mark)) {
        fwrite(rest, 1, (size_t)(hit - rest), out_asm);
        fputs(val, out_asm);
        rest = hit + strlen(mark);
    }
    fputs(rest, out_asm);
    /* main 返回后必须跳 halt：否则会落入紧跟在 call main 之后的 main 自身
     * 入口（call 的返回地址 = call 后的下一条指令 = main 开头），无限重入 main */
    fputs("\n    jmp halt\n", out_asm);
}

/* ============ 游戏汇编器语法兼容 pass：数据引用 → 绝对地址 ============
 *
 * 游戏内置汇编器对 U16 立即数的 label 支持不一致：jmp/条件跳转/call 接受
 * label（.isa: `(immediate | label)`），但 mov/load_32/store_32 的 imm
 * 变体只接受数字（.isa: `(immediate)`）——数据位置的符号引用（s0、cursor）
 * 会报 "not a register"。因此 codegen 发射数据引用时用 @name 哨兵，最后由
 * 本 pass 解析为绝对字节地址（label 定义处记录的偏移）。控制流 label
 * （jmp/je/call 目标）保留，游戏汇编器自己解析（游戏支持前向引用：
 * main 先于 putstr 定义、jmp halt 等均能工作）。
 *
 * 布局依据（与游戏 .isa 逐字一致，两处布局相同，偏移可靠）：
 * - 指令 4 字节；伪指令多词：push/pop = 8、call = 20、ret = 12
 * - 全局 = U32 4 字节；字符串 = 内容字节数 + 结尾 U8 0
 */

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* 标签映射：name → 字节偏移 */
typedef struct { char *name; long off; } ResLabel;
static ResLabel *rlabels;
static int nrlabels, crlabels;

static void rlabel_add(const char *name, size_t len, long off) {
    if (nrlabels == crlabels) {
        crlabels = crlabels ? crlabels * 2 : 64;
        rlabels = (ResLabel *)realloc(rlabels, (size_t)crlabels * sizeof *rlabels);
        if (!rlabels) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    rlabels[nrlabels].name = (char *)malloc(len + 1);
    if (!rlabels[nrlabels].name) { fprintf(stderr, "out of memory\n"); exit(1); }
    memcpy(rlabels[nrlabels].name, name, len);
    rlabels[nrlabels].name[len] = 0;
    rlabels[nrlabels].off = off;
    nrlabels++;
}

static int rlabel_find(const char *name, size_t len, long *off) {
    for (int i = 0; i < nrlabels; i++)
        if (strlen(rlabels[i].name) == len &&
            memcmp(rlabels[i].name, name, len) == 0) {
            *off = rlabels[i].off;
            return 1;
        }
    return 0;
}

/* 读一行（动态缓冲，支持长字符串数据行）；返回 0 = EOF */
static int read_line(FILE *in, char **bufp, size_t *cap) {
    size_t len = 0;
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (len + 2 > *cap) {
            *cap = *cap ? *cap * 2 : 256;
            *bufp = (char *)realloc(*bufp, *cap);
            if (!*bufp) { fprintf(stderr, "out of memory\n"); exit(1); }
        }
        (*bufp)[len++] = (char)c;
        if (c == '\n') break;
    }
    if (len == 0) return 0;
    (*bufp)[len] = 0;
    return 1;
}

/* 行为 label 定义（`name:` 结尾、无指令缩进）则返回名字长度，否则 0 */
static size_t label_name_len(const char *line) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        n--;
    if (n < 2 || line[n - 1] != ':')
        return 0;
    if (line[0] == ' ' || line[0] == ';' || line[0] == '"')
        return 0;
    for (size_t i = 0; i < n - 1; i++)
        if (!is_ident_char(line[i]))
            return 0;
    return n - 1;
}

/* 一行汇编的字节长度：label/注释/空行 = 0；指令 = 4；
 * 伪指令 push/pop/call/ret 是多词编码（8/8/20/12，游戏 .isa 与
 * asm.c 展开逐字一致）；U8/U16/U32 = 1/2/4；字符串行 = 内容字节数 */
static long line_size(const char *line) {
    const char *s = line;
    while (*s == ' ') s++;
    if (*s == ';' || *s == '\n' || *s == 0)
        return 0;
    if (label_name_len(line))       /* label 定义行（`name:`，无缩进）：0 字节 */
        return 0;
    if (*s == '"') {
        const char *q = strchr(s + 1, '"');
        if (!q) {
            fprintf(stderr, "codegen: 字符串数据行缺闭合引号: %s", line);
            exit(1);
        }
        return (long)(q - s - 1);
    }
    if (strncmp(s, "U8 ", 3) == 0) return 1;
    if (strncmp(s, "U16 ", 4) == 0) return 2;
    if (strncmp(s, "U32 ", 4) == 0) return 4;
    if (strncmp(s, "push ", 5) == 0) return 8;
    if (strncmp(s, "pop ", 4) == 0) return 8;
    if (strncmp(s, "call ", 5) == 0) return 20;
    if (strncmp(s, "ret", 3) == 0) return 12;
    return 4;
}

/* 把一行中的 @name 数据引用替换为绝对地址，写入 out；注释/字符串原样 */
static void emit_resolved_line(const char *line, FILE *out) {
    for (const char *p = line; *p; ) {
        if (*p == ';' || *p == '"') {           /* 注释与字符串数据行原样 */
            fputs(p, out);
            return;
        }
        if (*p == '@') {
            const char *name = p + 1;
            size_t nl = 0;
            while (is_ident_char(name[nl]))
                nl++;
            long off;
            if (!rlabel_find(name, nl, &off)) {
                fprintf(stderr, "codegen: 未解析的数据引用 @%.*s\n",
                        (int)nl, name);
                exit(1);
            }
            if (off > 0xFFFF) {
                fprintf(stderr, "codegen: 数据地址 0x%lx 超出 16 位寻址"
                        "（M1 程序过大）\n", off);
                exit(1);
            }
            fprintf(out, "0x%lx", off);
            p = name + nl;
        } else {
            fputc(*p, out);
            p++;
        }
    }
}

/* 两遍：先算所有 label 偏移，再替换 @ 引用。in = 已发射文本，out = 最终输出 */
static void resolve_refs(FILE *in, FILE *out) {
    char *line = NULL;
    size_t cap = 0;
    long loc = 0;

    rewind(in);
    while (read_line(in, &line, &cap)) {
        size_t nl = label_name_len(line);
        if (nl)
            rlabel_add(line, nl, loc);
        loc += line_size(line);
    }

    rewind(in);
    while (read_line(in, &line, &cap))
        emit_resolved_line(line, out);
    free(line);
}

bool codegen(Program *prog, FILE *out) {
    /* 先发射到临时文件，最后统一过布局 pass（数据引用 → 绝对地址）。
     * tmpfile() 在 Windows 上为二进制模式，不做 CRLF 翻译 */
    FILE *tmp = tmpfile();
    if (!tmp) { fprintf(stderr, "codegen: tmpfile failed\n"); exit(1); }
    out_asm = tmp;
    label_cnt = 0;
    nrlabels = 0;

    fprintf(out_asm, "; symcc 输出 — SymphonyPlus 汇编\n");

    emit_crt0();

    /* 函数顺序：main 必须最先（入口由 crt0 的 call main 决定，紧随其后
     * 保证 16 位相对寻址可达），其余按定义顺序（链表是逆序，恢复正序） */
    static Func *flist[256];
    int n = 0;
    for (Func *f = prog->funcs; f; f = f->next)
        if (f->len == 4 && strncmp(f->name, "main", 4) == 0)
            flist[n++] = f;
    for (Func *f = prog->funcs; f; f = f->next)
        if (!(f->len == 4 && strncmp(f->name, "main", 4) == 0))
            flist[n++] = f;
    if (n > 256) { fprintf(stderr, "codegen: too many functions\n"); exit(1); }
    for (int i = 0; i < n; i++)
        gen_func(flist[i]);

    /* 数据段放在代码之后（label 地址 = 代码后偏移，16 位寻址成立） */
    emit_data(prog);

    fprintf(out_asm, "\nhalt:\n");
    fprintf(out_asm, "    jmp halt\n");

    /* 布局 pass：@name 数据引用 → 绝对地址（游戏汇编器只认数字） */
    resolve_refs(tmp, out);
    fclose(tmp);
    return true;
}
