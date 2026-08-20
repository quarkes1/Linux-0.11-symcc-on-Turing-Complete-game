/* symcc/src/codegen.c — SymphonyPlus 汇编代码生成
 *
 * 约定：
 * - 表达式求值结果在 r1；r2 作二元运算第二操作数；r9 作地址暂存
 * - 二元运算：右操作数 → push 暂存 → 左操作数 → pop r2 → 运算（r1 op r2）
 *   （减法顺序自然满足 left - right：sub r1, r1, r2）
 * - 乘用 mul（无符号低 32 位与有符号一致）；int 全部按有符号（jl/jle/jg/jge）
 * - 比较/逻辑结果 materialize 为 0/1（label 跳转）；&&/|| 短路求值
 * - 局部变量：负偏移栈槽，无 base+offset 寻址 → sub r9, sp, |off| 合成地址
 * - 立即数上限 0xFFFF（ISA 硬限制）：超出直接报错，编译中止
 * - M1 无 crt0：main 直启，返回用 jmp halt（Task 9 正式 crt0 换 ret）
 */

#include <stdio.h>
#include <stdlib.h>

#include "symcc.h"

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
        fprintf(out_asm, "    load_32 r1, [r9]    ; var\n");
        return;
    case ND_ASSIGN:
        gen_expr(n->rhs);
        gen_addr(n->lhs);
        fprintf(out_asm, "    store_32 [r9], r1   ; =\n");
        return;
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
        case ND_ADD: fprintf(out_asm, "    add r1, r1, r2     ; +\n"); return;
        case ND_SUB: fprintf(out_asm, "    sub r1, r1, r2     ; -\n"); return;
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
        fprintf(out_asm, "    mov sp, r10        ; 恢复帧\n");
        fprintf(out_asm, "    jmp halt           ; M1 占位：无 crt0\n");
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

bool codegen(Node *prog, FILE *out) {
    out_asm = out;
    label_cnt = 0;
    fprintf(out, "; symcc 输出 — SymphonyPlus 汇编\n");
    fprintf(out, "main:\n");
    fprintf(out, "    mov sp, 0x4000       ; crt0 占位：初始化栈顶（Task 9 正式 crt0）\n");
    fprintf(out, "    mov r10, sp          ; 帧基址（push/pop 不改，变量偏移基准）\n");
    fprintf(out, "    sub sp, sp, %d        ; 帧：局部变量 %d 字节\n",
            symcc_frame_size(), symcc_frame_size());

    for (Node *n = prog; n; n = n->next)
        gen_stmt(n);

    fprintf(out, "halt:\n");
    fprintf(out, "    jmp halt\n");
    return true;
}
