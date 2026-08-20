/* symcc/src/codegen.c — SymphonyPlus 汇编代码生成
 *
 * 约定：
 * - 表达式求值结果在 r1
 * - 二元运算：右操作数 → push 暂存 → 左操作数 → pop 到 r2 → 运算（r1 op r2）
 *   （减法顺序自然满足 left - right：sub r1, r1, r2）
 * - 乘用 mul（无符号低 32 位与有符号一致）
 * - 立即数上限 0xFFFF（ISA 硬限制）：超出直接报错，编译中止
 * - 停机约定：文件尾部 halt: jmp halt（自跳转）
 */

#include <stdio.h>
#include <stdlib.h>

#include "symcc.h"

static FILE *out_asm;

static void err_imm(Token *t, int64_t val) {
    fprintf(stderr, "constant too large for 16-bit immediate: %lld (%.*s)\n",
            (long long)val, t->len, t->loc);
    exit(1);
}

/* 数字常量 → r1；>0xFFFF 报错 */
static void gen_num(int64_t val, Token *t) {
    if (val > 0xFFFF || val < 0)
        err_imm(t, val);
    fprintf(out_asm, "    mov r1, %lld        ; %lld\n", (long long)val, (long long)val);
}

static void gen_expr(Node *n) {
    switch (n->kind) {
    case ND_NUM:
        gen_num(n->val, n->tok);
        return;
    case ND_NEG:
        gen_expr(n->lhs);
        fprintf(out_asm, "    neg r1, r1         ; unary -\n");
        return;
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
        /* 右操作数 → push → 左操作数 → pop r2 → r1 op r2 */
        gen_expr(n->rhs);
        fprintf(out_asm, "    push r1            ; 暂存右操作数\n");
        gen_expr(n->lhs);
        fprintf(out_asm, "    pop r2             ; 取回右操作数\n");
        switch (n->kind) {
        case ND_ADD: fprintf(out_asm, "    add r1, r1, r2     ; +\n"); break;
        case ND_SUB: fprintf(out_asm, "    sub r1, r1, r2     ; -\n"); break;
        case ND_MUL: fprintf(out_asm, "    mul r1, r1, r2     ; *\n"); break;
        }
        return;
    default:
        fprintf(stderr, "codegen: unhandled node kind %d\n", n->kind);
        exit(1);
    }
}

bool codegen(Node *prog, FILE *out) {
    out_asm = out;
    fprintf(out, "; symcc 输出 — SymphonyPlus 汇编\n");
    fprintf(out, "main:\n");
    fprintf(out, "    mov sp, 0x4000       ; crt0 占位：初始化栈顶（Task 9 正式 crt0）\n");
    fprintf(out, "    sub sp, sp, 0        ; 帧空间（M1：0 字节）\n");

    for (Node *n = prog; n; n = n->next) {
        if (n->kind == ND_RETURN) {
            gen_expr(n->lhs);
            fprintf(out, "    ; return\n");
            fprintf(out, "    add sp, sp, 0\n");
            fprintf(out, "    jmp halt           ; M1 占位：无 crt0，ret 的 jmp flags 会跳到脏地址\n");
        } else {
            fprintf(stderr, "codegen: unexpected statement kind %d\n", n->kind);
            return false;
        }
    }

    fprintf(out, "halt:\n");
    fprintf(out, "    jmp halt\n");
    return true;
}
