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
 * - 立即数上限 0xFFFF（ISA 硬限制）：超出直接报错，编译中止
 * - M1 无 crt0：main 直启（mov sp,0x4000），main 返回用 jmp halt
 *   （Task 9 正式 crt0 换 ret 链路）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"

static FILE *out_asm;
static int label_cnt;
static bool in_main;   /* 当前生成函数是否是 main（return 处理不同） */

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

/* 数字常量 → r1；>0xFFFF 报错 */
static void gen_num(int64_t val, Token *t) {
    if (val > 0xFFFF || val < 0)
        err_imm(t, val);
    fprintf(out_asm, "    mov r1, %lld        ; %lld\n", (long long)val, (long long)val);
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
            fprintf(out_asm, "    load_8 r1, [%s]     ; global char\n", n->name);
        else
            fprintf(out_asm, "    load_32 r1, [%s]    ; global\n", n->name);
        if (n->ty->kind == TY_CHAR) {
            fprintf(out_asm, "    lsl r1, r1, 24\n");
            fprintf(out_asm, "    %s r1, r1, 24   ; %s扩展\n",
                    n->ty->is_unsigned ? "lsr" : "asr",
                    n->ty->is_unsigned ? "零" : "符号");
        }
        return;
    case ND_STR:
        fprintf(out_asm, "    mov r1, s%lld       ; string\n", (long long)n->val);
        return;
    case ND_ADDR:
        /* 地址 → r1：局部 = r10-|offset|；全局/字符串 = label 地址 */
        if (n->lhs->kind == ND_VAR)
            fprintf(out_asm, "    sub r1, r10, %d    ; addr of var\n", -n->lhs->offset);
        else if (n->lhs->kind == ND_GVAR)
            fprintf(out_asm, "    mov r1, %s         ; addr of global\n", n->lhs->name);
        else
            fprintf(out_asm, "    mov r1, s%lld       ; addr of string\n", (long long)n->lhs->val);
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
                fprintf(out_asm, "    store_8 [%s], r1   ; = global char\n", n->lhs->name);
            else
                fprintf(out_asm, "    store_32 [%s], r1   ; = global\n", n->lhs->name);
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
        if (in_main) {
            fprintf(out_asm, "    mov sp, r10        ; 恢复帧\n");
            fprintf(out_asm, "    jmp halt           ; M1 占位：无 crt0\n");
        } else {
            fprintf(out_asm, "    mov sp, r10\n");
            fprintf(out_asm, "    pop r10            ; 恢复调用方帧指针\n");
            fprintf(out_asm, "    ret\n");
        }
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

/* 函数尾言（体末尾无 return 时的兜底） */
static void gen_epilogue(void) {
    if (in_main) {
        fprintf(out_asm, "    jmp halt           ; main 无 return 兜底\n");
    } else {
        fprintf(out_asm, "    mov sp, r10\n");
        fprintf(out_asm, "    pop r10            ; 恢复调用方帧指针\n");
        fprintf(out_asm, "    ret\n");
    }
}

/* 函数序言 + 体 + 尾言 */
static void gen_func(Func *f) {
    in_main = (f->len == 4 && strncmp(f->name, "main", 4) == 0);

    fprintf(out_asm, "\n%s:\n", f->name);
    if (in_main) {
        fprintf(out_asm, "    mov sp, 0x4000       ; crt0 占位：初始化栈顶（Task 9 正式 crt0）\n");
    } else {
        fprintf(out_asm, "    push r10             ; 保存调用方帧指针\n");
    }
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

bool codegen(Program *prog, FILE *out) {
    out_asm = out;
    label_cnt = 0;

    fprintf(out, "; symcc 输出 — SymphonyPlus 汇编\n");

    /* 函数顺序：main 必须最先（模拟器从地址 0 执行；数据段不得挡在入口），
     * 其余按定义顺序（链表是逆序，恢复正序） */
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
    return true;
}
