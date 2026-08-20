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
#include <stdarg.h>
#include <string.h>

#include "symcc.h"
#include "obj.h"
#include "symcc/include/config.h"

#define XSTR(x) STR(x)
#define STR(x) #x

static Obj *cur_obj;       /* 当前对象（codegen 输出目标） */
static bool obj_d32;       /* d32：数据引用拆 32 位装载 */
static bool in_data;       /* 当前段：true=.data（emit 分派） */
static int label_cnt;
static Func *cur_func;   /* 当前生成函数（__builtin_va_start / 参数复制用） */

/* break/continue 目标标签栈（循环/switch 压入，gen_stmt 消费） */
static const char *brk_lbls[64];
static int brk_depth;
static const char *cont_lbls[64];
static int cont_depth;

static void emit(const char *fmt, ...);   /* 定义在 gen_block_copy 之后 */

/* 块拷贝：r1 = 源地址（低地址），r9 = 目标地址，按 4 字节块复制 size 字节。
 * 约定使用 r2/r3 作暂存（本 helper 内部不调用其他 gen_*，无嵌套冲突） */
static void gen_block_copy(int64_t size) {
    for (int64_t off = 0; off + 4 <= size; off += 4) {
        emit("    add r3, r1, %d      ; copy src\n", (int)off);
        emit("    load_32 r2, [r3]\n");
        emit("    add r3, r9, %d      ; copy dst\n", (int)off);
        emit("    store_32 [r3], r2\n");
    }
}

/* 类型是否按值传聚合（struct/union）：是 → 块拷贝，否 → 4 字节标量 */
static bool is_agg(Type *t) {
    return t->kind == TY_STRUCT || t->kind == TY_UNION;
}

/* 段感知输出：.text 行进 obj_add_text，.data 行进 obj_add_data。
 * 行内容不含换行（链接器按行处理；序列化统一补 \n）——标签行前导
 * 空行（\n）剥离：空行对布局无意义（line_size 0），只留纯标签行 */
static void emit(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = 0;
    size_t i = 0;
    while (buf[i] == '\n' || buf[i] == '\r')
        i++;
    if (i)
        memmove(buf, buf + i, n - i + 1);
    if (in_data)
        obj_add_data(cur_obj, buf);
    else
        obj_add_text(cur_obj, buf);
}

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
    emit("%s:\n", l);
}

/* 数字常量 → 指定寄存器。
 * 立即数上限 16 位：>0xFFFF 拆高位/低位拼接（mov hi; lsl 16; or lo），
 * 上限 0xFFFFFFFF（M2 无 64 位） */
static void gen_num_reg(int reg, int64_t val, Token *t) {
    if (val > 0xFFFFFFFFLL || val < 0)
        err_imm(t, val);
    int64_t hi = (val >> 16) & 0xFFFF;
    int64_t lo = val & 0xFFFF;
    if (hi) {
        emit("    mov r%d, %lld        ; 常量高位\n", reg, (long long)hi);
        emit("    lsl r%d, r%d, 16\n", reg, reg);
        if (lo)
            emit("    or r%d, r%d, %lld     ; 常量低位\n", reg, reg, (long long)lo);
    } else {
        emit("    mov r%d, %lld        ; %lld\n", reg, (long long)lo, (long long)val);
    }
}

static void gen_num(int64_t val, Token *t) {
    gen_num_reg(1, val, t);
}

static void gen_expr(Node *n);
static void gen_member_addr(Node *n);
static void gen_laddr(Node *n);
static void gen_branch_zero(Node *cond, const char *label, bool jump_if_zero);

/* 左值地址 → r9。局部变量相对帧基址 r10（函数入口 sp）——push/pop 会
 * 改变 sp，若相对 sp 则压栈后变量错位；全局/字符串 = label 地址。 */
static void gen_laddr(Node *n) {
    switch (n->kind) {
    case ND_VAR:
        emit("    sub r9, r10, %d    ; addr of var\n", -n->offset);
        return;
    case ND_GVAR:
        if (obj_d32) {
            /* 32 位数据地址：mov @hi + lsl 16 + or @lo */
            emit("    mov r9, @hi:%s    ; addr of global (hi)\n", n->name);
            emit("    lsl r9, r9, 16\n");
            emit("    or r9, r9, @lo:%s ; addr of global (lo)\n", n->name);
        } else {
            emit("    mov r9, @%s        ; addr of global\n", n->name);
        }
        return;
    case ND_DEREF:
        gen_expr(n->lhs);
        emit("    mov r9, r1          ; deref addr\n");
        return;
    case ND_MEMBER:
        gen_member_addr(n);
        return;
    default:
        fprintf(stderr, "codegen: not an lvalue\n");
        exit(1);
    }
}

/* 成员地址 → r9：聚合对象地址（或指针值）+ 字节偏移 */
static void gen_member_addr(Node *n) {
    if (n->lhs->kind == ND_DEREF) {
        gen_expr(n->lhs->lhs);             /* 指针值（p->m 展开） */
        if (n->val)
            emit("    add r9, r1, %d      ; member +%lld\n", (int)n->val, (long long)n->val);
        else
            emit("    mov r9, r1          ; member\n");
    } else if (n->lhs->kind == ND_CALL) {
        /* f().m：f() 的 struct 结果地址在 r1（非左值，不能 gen_laddr） */
        gen_expr(n->lhs);
        if (n->val)
            emit("    add r9, r1, %d      ; member +%lld\n", (int)n->val, (long long)n->val);
        else
            emit("    mov r9, r1          ; member\n");
    } else {
        gen_laddr(n->lhs);
        if (n->val)
            emit("    add r9, r9, %d      ; member +%lld\n", (int)n->val, (long long)n->val);
    }
}

/* 位域加载：r9 = 单元地址 → r1 = 位域值（符号/零扩展）。
 * 位域从单元最高位（bit_offset=0）起打包：值 = (单元 >> (32-bo-bw)) & mask */
static void gen_load_bitfield(Node *n) {
    emit("    load_32 r1, [r9]    ; bitfield unit\n");
    int sh = 32 - n->bit_offset - n->bit_width;
    if (sh > 0) {
        gen_num_reg(2, sh, n->tok);
        emit("    lsr r1, r1, r2\n");
    }
    int64_t mask = (n->bit_width >= 32) ? 0xFFFFFFFFLL
                                        : ((1LL << n->bit_width) - 1);
    if (mask != 0xFFFFFFFFLL) {
        gen_num_reg(2, mask, n->tok);
        emit("    and r1, r1, r2\n");
    }
    if (!n->ty->is_unsigned && n->bit_width < 32) {
        /* 有符号位域：符号扩展 */
        emit("    lsl r1, r1, %d\n", 32 - n->bit_width);
        emit("    asr r1, r1, %d\n", 32 - n->bit_width);
    }
}

/* 位域存储：r9 = 单元地址，r1 = 值 → 读-改-写单元 */
static void gen_store_bitfield(Node *n) {
    int sh = 32 - n->bit_offset - n->bit_width;
    int64_t mask = (n->bit_width >= 32) ? 0xFFFFFFFFLL
                                        : ((1LL << n->bit_width) - 1);
    emit("    load_32 r2, [r9]    ; bitfield unit\n");
    gen_num_reg(3, mask, n->tok);
    emit("    and r1, r1, r3      ; 值截断\n");
    if (sh > 0)
        emit("    lsl r1, r1, %d\n", sh);
    if (sh > 0) {
        emit("    lsl r3, r3, %d      ; mask<<shift\n", sh);
    }
    gen_num_reg(4, 0xFFFFFFFFLL, n->tok);
    emit("    xor r3, r3, r4      ; ~(mask<<shift)\n");
    emit("    and r2, r2, r3      ; 清目标位\n");
    emit("    or r2, r2, r1       ; 组合\n");
    emit("    store_32 [r9], r2\n");
}

/* 按类型从 [r9] 加载到 r1：char 符号扩展（unsigned char 零扩展） */
static void gen_load(Type *t) {
    if (t->kind == TY_CHAR) {
        emit("    load_8 r1, [r9]     ; char\n");
        emit("    lsl r1, r1, 24\n");
        emit("    %s r1, r1, 24   ; %s扩展\n",
                t->is_unsigned ? "lsr" : "asr", t->is_unsigned ? "零" : "符号");
    } else {
        emit("    load_32 r1, [r9]    ; load\n");
    }
}

/* 按类型把 r1 存入 [r9]：char 用 store_8（天然截断低 8 位） */
static void gen_store(Type *t) {
    if (t->kind == TY_CHAR)
        emit("    store_8 [r9], r1    ; char\n");
    else
        emit("    store_32 [r9], r1   ; store\n");
}

/* 比较 materialize：r1 = (r1 op r2) ? 1 : 0；jmp_op = 真时跳转的助记符 */
static void gen_compare(const char *jmp_op, const char *opname) {
    const char *Ltrue = new_label();
    const char *Lend = new_label();
    emit("    cmp r1, r2\n");
    emit("    %s %s        ; %s\n", jmp_op, Ltrue, opname);
    emit("    mov r1, 0\n");
    emit("    jmp %s\n", Lend);
    emit_label(Ltrue);
    emit("    mov r1, 1\n");
    emit_label(Lend);
}

static void gen_expr(Node *n);

/* 短路逻辑：r1 = lhs && rhs（右操作数可能不执行） */
static void gen_logand(Node *n) {
    const char *Lfalse = new_label();
    const char *Lend = new_label();
    gen_expr(n->lhs);
    emit("    mov r2, 0\n");
    emit("    cmp r1, r2\n");
    emit("    je %s        ; && 短路\n", Lfalse);
    gen_expr(n->rhs);
    emit("    cmp r1, r2\n");
    emit("    je %s\n", Lfalse);
    emit("    mov r1, 1\n");
    emit("    jmp %s\n", Lend);
    emit_label(Lfalse);
    emit("    mov r1, 0\n");
    emit_label(Lend);
}

static void gen_logor(Node *n) {
    const char *Ltrue = new_label();
    const char *Lend = new_label();
    gen_expr(n->lhs);
    emit("    mov r2, 0\n");
    emit("    cmp r1, r2\n");
    emit("    jne %s        ; || 短路\n", Ltrue);
    gen_expr(n->rhs);
    emit("    cmp r1, r2\n");
    emit("    jne %s\n", Ltrue);
    emit("    mov r1, 0\n");
    emit("    jmp %s\n", Lend);
    emit_label(Ltrue);
    emit("    mov r1, 1\n");
    emit_label(Lend);
}

static void gen_expr(Node *n) {
    switch (n->kind) {
    case ND_NUM:
        gen_num(n->val, n->tok);
        return;
    case ND_VAR:
    case ND_GVAR:
        /* 聚合（数组/结构体/联合）表达式求值 = 地址；标量加载值 */
        if (n->ty->kind == TY_ARRAY || n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION) {
            gen_laddr(n);
            emit("    mov r1, r9          ; aggregate addr\n");
            return;
        }
        gen_laddr(n);
        gen_load(n->ty);
        return;
    case ND_FUNC:
        emit("    mov r1, @%s        ; func addr\n", n->name);
        return;
    case ND_STR:
        emit("    mov r1, @s%lld      ; string\n", (long long)n->val);
        return;
    case ND_ADDR:
        if (n->lhs->kind == ND_STR) {
            emit("    mov r1, @s%lld     ; addr of string\n", (long long)n->lhs->val);
            return;
        }
        gen_laddr(n->lhs);
        emit("    mov r1, r9          ; addr\n");
        return;
    case ND_DEREF:
        gen_expr(n->lhs);
        if (n->ty->kind != TY_ARRAY && n->ty->kind != TY_STRUCT && n->ty->kind != TY_UNION) {
            emit("    mov r9, r1          ; deref addr\n");
            gen_load(n->ty);
        }
        return;
    case ND_MEMBER:
        gen_member_addr(n);
        if (n->bit_width >= 0)
            gen_load_bitfield(n);
        else
            gen_load(n->ty);
        return;
    case ND_VASTART:
        /* AP = 参数区起始 + 固定参数区大小 = r10 + 8 + 4*nfixed
         * （struct 返回函数隐藏 arg0 占 [r10+8]，固定参数后移 4） */
        gen_laddr(n->lhs);               /* r9 = AP 槽地址 */
        emit("    push r9            ; va_list slot\n");
        emit("    mov r9, r10\n");
        emit("    add r9, r9, %d    ; va_start\n",
                8 + 4 * cur_func->nargs +
                (cur_func->has_retbuf
                     ? (int)(((size_t)cur_func->fty->base->size + 3) & ~3)
                     : 0));
        emit("    pop r2             ; va_list slot\n");
        emit("    store_32 [r2], r9\n");
        return;
    case ND_CAST:
        gen_expr(n->lhs);
        if (n->ty->kind == TY_CHAR) {
            /* 截断到 8 位 + 符号/零扩展 */
            emit("    lsl r1, r1, 24     ; cast char\n");
            emit("    %s r1, r1, 24   ; %s扩展\n",
                    n->ty->is_unsigned ? "lsr" : "asr",
                    n->ty->is_unsigned ? "零" : "符号");
        }
        /* 其余转换（int↔指针↔unsigned）同宽无操作 */
        return;
    case ND_BITNOT:
        gen_expr(n->lhs);
        gen_num_reg(2, 0xFFFFFFFFLL, n->tok);
        emit("    xor r1, r1, r2     ; ~\n");
        return;
    case ND_ASSIGN:
        gen_laddr(n->lhs);           /* 先求左值地址（无副作用） */
        emit("    push r9            ; 暂存地址\n");
        gen_expr(n->rhs);            /* struct → r1 = 源地址 */
        emit("    pop r9             ; 取回目标地址\n");
        if (is_agg(n->ty)) {
            gen_block_copy(n->ty->size);
        } else if (n->lhs->bit_width >= 0) {
            gen_store_bitfield(n->lhs);
        } else {
            gen_store(n->ty);
        }
        return;
    case ND_CALL: {
        /* 实参从右往左求值（全栈传参）→ call → 清理。
         * struct/union 实参：块拷贝压栈（sub sp + 逐块 store）；
         * struct 返回函数：隐藏首参数（最后 push）= 返回缓冲区地址 */
        Node *args[64];
        int na = 0;
        for (Node *a = n->rhs; a; a = a->next)
            args[na++] = a;
        bool retbuf = is_agg(n->ty);
        int64_t total = 0;
        for (int k = na - 1; k >= 0; k--) {
            gen_expr(args[k]);
            if (is_agg(args[k]->ty)) {
                int64_t sz = args[k]->ty->size;
                emit("    sub sp, sp, %d      ; struct arg %d\n", (int)sz, k);
                emit("    mov r9, sp\n");
                gen_block_copy(sz);
                total += sz;
            } else {
                emit("    push r1            ; arg %d\n", k);
                total += 4;
            }
        }
        if (retbuf) {
            /* 返回缓冲区 = 隐藏 arg0（本体占 [r10+8]，用户参数后移对齐） */
            emit("    sub sp, sp, %d      ; retbuf\n", (int)n->ty->size);
            total += n->ty->size;
        }
        if (n->name) {
            emit("    call %s\n", n->name);
        } else {
            /* 动态调用（函数指针）：游戏无 call reg，展开为
             * counter r2; add r2,r2,20; push r2; jmp r1 —— 与静态
             * call 等长（20 字节），栈布局一致 */
            gen_expr(n->lhs);            /* r1 = 函数地址 */
            emit("    counter r2         ; 自身指令地址\n");
            emit("    add r2, r2, 20     ; 越过 push 与 jmp\n");
            emit("    push r2            ; 返回地址\n");
            emit("    jmp r1             ; 动态调用\n");
        }
        if (retbuf) {
            /* 表达式结果 = retbuf 本体地址（sp 即缓冲起点） */
            emit("    mov r1, sp          ; struct result addr\n");
        }
        if (total > 0)
            emit("    add sp, sp, %d      ; 清理实参\n", (int)total);
        return;
    }
    case ND_NEG:
        gen_expr(n->lhs);
        emit("    neg r1, r1         ; unary -\n");
        return;
    case ND_NOT:
        /* !x == (x == 0) */
        gen_expr(n->lhs);
        emit("    mov r2, 0\n");
        gen_compare("je", "!");
        return;
    case ND_LOGAND:
        gen_logand(n);
        return;
    case ND_LOGOR:
        gen_logor(n);
        return;
    case ND_ADD:
        /* 指针算术已在 parse 期缩放（p+n → n×elem），此处纯加 */
        gen_expr(n->rhs);
        emit("    push r1            ; 暂存右操作数\n");
        gen_expr(n->lhs);
        emit("    pop r2             ; 取回右操作数\n");
        emit("    add r1, r1, r2     ; +\n");
        return;
    case ND_SUB:
        gen_expr(n->rhs);
        emit("    push r1            ; 暂存右操作数\n");
        gen_expr(n->lhs);
        emit("    pop r2             ; 取回右操作数\n");
        emit("    sub r1, r1, r2     ; -\n");
        if (n->val) {
            /* 指针差：÷元素大小（n->val = elem size，parse 期设置）。
             * 1 无操作、2/4/8 移位、其他调用 __divsi3 */
            if (n->val == 2)
                emit("    asr r1, r1, 1      ; ptr diff /2\n");
            else if (n->val == 4)
                emit("    asr r1, r1, 2      ; ptr diff /4\n");
            else if (n->val == 8)
                emit("    asr r1, r1, 3      ; ptr diff /8\n");
            else if (n->val != 1) {
                emit("    push r1            ; 暂存差值\n");
                gen_num(n->val, n->tok);
                emit("    push r1            ; arg1 = 元素大小\n");
                emit("    pop r2\n");
                emit("    pop r1\n");
                emit("    push r2\n");
                emit("    push r1\n");
                emit("    call __divsi3      ; ptr diff /elem\n");
                emit("    add sp, sp, 8\n");
            }
        }
        return;
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_GT:
    case ND_GE:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
    case ND_LSL:
    case ND_LSR:
        /* 右操作数 → push → 左操作数 → pop r2 → r1 op r2 */
        gen_expr(n->rhs);
        emit("    push r1            ; 暂存右操作数\n");
        gen_expr(n->lhs);
        emit("    pop r2             ; 取回右操作数\n");
        switch (n->kind) {
        case ND_MUL: emit("    mul r1, r1, r2     ; *\n"); return;
        case ND_DIV:
        case ND_MOD:
            if (n->ty->is_unsigned) {
                /* 硬件 div/mod 即无符号：直接指令 */
                emit("    %s r1, r1, r2     ; %s（无符号）\n",
                        n->kind == ND_DIV ? "div" : "mod",
                        n->kind == ND_DIV ? "/" : "%");
            } else {
                /* 有符号：调用运行时（全栈传参：arg0=左、arg1=右） */
                emit("    push r2            ; arg1 = 右操作数\n");
                emit("    push r1            ; arg0 = 左操作数\n");
                emit("    call __%s\n",
                        n->kind == ND_DIV ? "divsi3" : "modsi3");
                emit("    add sp, sp, 8      ; 清理实参\n");
            }
            return;
        case ND_BITAND: emit("    and r1, r1, r2     ; &\n"); return;
        case ND_BITOR:  emit("    or r1, r1, r2      ; |\n"); return;
        case ND_BITXOR: emit("    xor r1, r1, r2     ; ^\n"); return;
        case ND_LSL:    emit("    lsl r1, r1, r2     ; <<\n"); return;
        case ND_LSR:
            /* unsigned 右移 lsr（逻辑），有符号 asr（算术） */
            emit("    %s r1, r1, r2     ; >>（%s）\n",
                    n->ty->is_unsigned ? "lsr" : "asr",
                    n->ty->is_unsigned ? "无符号" : "有符号");
            return;
        case ND_EQ:  gen_compare("je", "=="); return;
        case ND_NE:  gen_compare("jne", "!="); return;
        case ND_LT:
            gen_compare(n->ty->is_unsigned ? "jb" : "jl", "<"); return;
        case ND_LE:
            gen_compare(n->ty->is_unsigned ? "jbe" : "jle", "<="); return;
        case ND_GT:
            gen_compare(n->ty->is_unsigned ? "ja" : "jg", ">"); return;
        case ND_GE:
            gen_compare(n->ty->is_unsigned ? "jae" : "jge", ">="); return;
        }
        return;
    case ND_COND: {
        /* ?: 条件 → 跳两臂，值在 r1 */
        const char *Lelse = new_label();
        const char *Lend = new_label();
        gen_branch_zero(n->lhs, Lelse, true);
        gen_expr(n->rhs);
        emit("    jmp %s\n", Lend);
        emit_label(Lelse);
        if (n->els)
            gen_expr(n->els);
        emit_label(Lend);
        return;
    }
    case ND_COMMA:
        gen_expr(n->lhs);        /* 求值丢弃 */
        gen_expr(n->rhs);        /* 值 = 右 */
        return;
    default:
        fprintf(stderr, "codegen: unhandled node kind %d\n", n->kind);
        exit(1);
    }
}

/* 条件：r1 = 0/1 时跳转 op_label（等于 0 跳 je，非 0 跳 jne） */
static void gen_branch_zero(Node *cond, const char *label, bool jump_if_zero) {
    gen_expr(cond);
    emit("    mov r2, 0\n");
    emit("    cmp r1, r2\n");
    emit("    %s %s\n", jump_if_zero ? "je" : "jne", label);
}

static void gen_stmt(Node *n);

/* 语句链表：块 { ... } 产生链表，必须全部生成（曾只生成头语句，
 * 导致 while/if 体内第二条语句被丢弃 → i++ 缺失 → 死循环） */
static void gen_stmts(Node *n) {
    for (; n; n = n->next) {
        gen_stmt(n);
    }
}

static void gen_stmt(Node *n) {
    switch (n->kind) {
    case ND_RETURN:
        if (n->lhs && is_agg(n->lhs->ty)) {
            /* struct/union 返回：拷贝到隐藏返回缓冲区 [r10+8] */
            gen_expr(n->lhs);            /* r1 = 源地址 */
            emit("    add r9, r10, 8       ; retbuf\n");
            gen_block_copy(n->lhs->ty->size);
        } else {
            gen_expr(n->lhs);
        }
        emit("    ; return\n");
        emit("    mov sp, r10\n");
        emit("    pop r10            ; 恢复调用方帧指针\n");
        emit("    ret\n");
        return;
    case ND_IF: {
        const char *Lelse = new_label();
        const char *Lend = new_label();
        gen_branch_zero(n->lhs, Lelse, true);
        gen_stmts(n->rhs);
        if (n->els) {
            emit("    jmp %s\n", Lend);
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
        /* continue → 条件处（Lbegin）；break → Lend */
        brk_lbls[brk_depth++] = Lend;
        cont_lbls[cont_depth++] = Lbegin;
        emit_label(Lbegin);
        gen_branch_zero(n->lhs, Lend, true);
        gen_stmts(n->rhs);
        emit("    jmp %s\n", Lbegin);
        emit_label(Lend);
        cont_depth--;
        brk_depth--;
        return;
    }
    case ND_FOR: {
        const char *Lbegin = new_label();
        const char *Lcont = new_label();
        const char *Lend = new_label();
        if (n->lhs)
            gen_expr(n->lhs);
        emit_label(Lbegin);
        if (n->rhs)
            gen_branch_zero(n->rhs, Lend, true);
        brk_lbls[brk_depth++] = Lend;
        cont_lbls[cont_depth++] = Lcont;
        gen_stmts(n->body);
        cont_depth--;
        brk_depth--;
        emit_label(Lcont);            /* continue → 增量处 */
        if (n->els)
            gen_expr(n->els);
        emit("    jmp %s\n", Lbegin);
        emit_label(Lend);
        return;
    }
    case ND_DOWHILE: {
        const char *Lbegin = new_label();
        const char *Lcond = new_label();
        const char *Lend = new_label();
        brk_lbls[brk_depth++] = Lend;
        cont_lbls[cont_depth++] = Lcond;   /* continue → 条件处 */
        emit_label(Lbegin);
        gen_stmts(n->lhs);
        emit_label(Lcond);
        gen_branch_zero(n->rhs, Lend, true);
        emit("    jmp %s\n", Lbegin);
        emit_label(Lend);
        cont_depth--;
        brk_depth--;
        return;
    }
    case ND_SWITCH: {
        /* 条件 → 隐藏槽 → 体（第一个 case 前的语句无条件执行）→
         * 顺序比较链 → 各 case 标签 + 剩余体 → 出口 */
        const char *Lend = new_label();
        char *Ldef = NULL;
        gen_expr(n->lhs);
        emit("    sub r9, r10, %d    ; switch cond slot\n", n->offset);
        emit("    store_32 [r9], r1\n");
        /* 第一个 case/default 之前的语句（无条件执行） */
        Node *pre = n->els;
        while (pre && pre->kind != ND_CASE && pre->kind != ND_DEFAULT) {
            gen_stmt(pre);
            pre = pre->next;
        }
        /* 比较链 */
        for (Node *c = n->rhs; c; c = c->next) {
            if (c->kind == ND_DEFAULT) {
                Ldef = (char *)malloc(32);
                snprintf(Ldef, 32, "Lg%d", c->num);
                continue;
            }
            emit("    sub r9, r10, %d    ; switch cond\n", n->offset);
            emit("    load_32 r1, [r9]\n");
            gen_num_reg(2, c->val, c->tok);
            emit("    cmp r1, r2\n");
            emit("    je Lg%d\n", c->num);
        }
        emit("    jmp %s\n", Ldef ? Ldef : Lend);
        brk_lbls[brk_depth++] = Lend;
        gen_stmts(pre);        /* 体：case 标签 + 语句顺序生成，fallthrough 天然 */
        brk_depth--;
        emit_label(Lend);
        free(Ldef);
        return;
    }
    case ND_CASE:
    case ND_DEFAULT: {
        char buf[32];
        snprintf(buf, sizeof buf, "Lg%d", n->num);
        emit_label(buf);
        return;
    }
    case ND_BREAK:
        emit("    jmp %s\n", brk_lbls[brk_depth - 1]);
        return;
    case ND_CONTINUE:
        emit("    jmp %s\n", cont_lbls[cont_depth - 1]);
        return;
    case ND_GOTO:
        emit("    jmp Lg%d\n", n->num);
        return;
    case ND_LABEL: {
        emit("Lg%d:\n", n->num);
        gen_stmts(n->rhs);
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
    emit("    mov sp, r10\n");
    emit("    pop r10            ; 恢复调用方帧指针\n");
    emit("    ret\n");
}

/* 函数序言 + 体 + 尾言 */
static void gen_func(Func *f) {
    cur_func = f;
    emit("\n%s:\n", f->name);
    /* 非 static 函数导出符号（offset 由链接器布局时重算，这里传 0） */
    if (!f->is_static)
        obj_add_symbol(cur_obj, f->name, false, 0, 0, 0);
    emit("    push r10             ; 保存调用方帧指针\n");
    emit("    mov r10, sp          ; 帧基址\n");
    if (f->frame_size)
        emit("    sub sp, sp, %d        ; 帧：局部变量 %d 字节\n",
                f->frame_size, f->frame_size);

    /* 参数拷入栈槽：实参 k 在 [r10+8+参数区累积偏移]（序言 push r10 后
     * 入口 sp = r10）；struct 返回函数隐藏 arg0 = 返回缓冲区本体，占
     * [r10+8]（对齐），用户参数从 [r10+8+对齐大小] 起。栈槽按参数大小
     * 对齐分配（与 parse 的 locals_bytes 累积一致）。 */
    int arg_off = 8 + (f->has_retbuf
                           ? (int)(((size_t)f->fty->base->size + 3) & ~3)
                           : 0);
    int slot_off = 0;
    for (int k = 0; k < f->nargs; k++) {
        int64_t psz = ((size_t)f->param_tys[k]->size + 3) & ~3;
        slot_off += (int)psz;
        if (is_agg(f->param_tys[k])) {
            emit("    add r1, r10, %d        ; arg %d src\n", arg_off, k);
            emit("    sub r9, r10, %d        ; arg %d slot\n", slot_off, k);
            gen_block_copy(f->param_tys[k]->size);
        } else {
            emit("    add r9, r10, %d        ; arg %d addr\n", arg_off, k);
            emit("    load_32 r1, [r9]\n");
            emit("    sub r9, r10, %d        ; arg %d slot\n", slot_off, k);
            emit("    store_32 [r9], r1\n");
        }
        arg_off += (int)psz;
    }

    gen_stmts(f->body);
    gen_epilogue();
}

/* 数据段：全局变量（label = 名字，init_data 字节大端发射）+ 字符串（label = s%d） */
static void emit_data(Program *prog) {
    for (Global *g = prog->globals; g; g = g->next) {
        /* extern：引用外部定义，不分配（引用经 @name 链接解析） */
        if (g->is_extern)
            continue;
        if (!g->init_data || g->init_data_len == 0) {
            /* 未初始化：进 .bss（链接器在数据段后合并分配） */
            obj_add_bss(cur_obj, g->name, g->ty->size);
            if (!g->is_static)
                obj_add_symbol(cur_obj, g->name, true, 1, 0, g->ty->size);
            continue;
        }
        emit("\n%s:\n", g->name);
        /* 非 static 数据符号导出（offset 由链接器重算） */
        if (!g->is_static)
            obj_add_symbol(cur_obj, g->name, true, 1, 0, g->ty->size);
        int n = g->init_data_len;
        int off = 0;
        for (; off + 4 <= n; off += 4) {
            /* 字符串 reloc 槽 → 引用 label @s%d（resolve_refs 解析为地址） */
            int sidx = -1;
            for (int k = 0; k < g->n_str_relocs; k++)
                if (g->str_relocs[2 * k] == off) {
                    sidx = g->str_relocs[2 * k + 1];
                    break;
                }
            if (sidx >= 0) {
                emit("    U32 @s%d\n", sidx);
                continue;
            }
            /* 函数地址 reloc 槽 → 引用 label @name */
            const char *fname = NULL;
            for (int k = 0; k < g->n_func_relocs; k++)
                if (g->func_reloc_offsets[k] == off) {
                    fname = g->func_reloc_names[k];
                    break;
                }
            if (fname) {
                emit("    U32 @%s\n", fname);
                continue;
            }
            emit("    U32 0x%02x%02x%02x%02x\n",
                    g->init_data[off], g->init_data[off + 1],
                    g->init_data[off + 2], g->init_data[off + 3]);
        }
        for (; off < n; off++)
            emit("    U8 %d\n", g->init_data[off]);
    }
    int idx = 0;
    for (Token *t = prog->strs; t; t = t->next) {
        emit("\ns%d:\n", idx++);
        /* 含 NUL 或引号的字符串不能用 "..." 原样发射（汇编器按引号/字节扫），
         * 退回逐字节 U8；其余用 "..." + 结尾 U8 0 */
        if (memchr(t->str, 0, (size_t)t->str_len) ||
            memchr(t->str, '"', (size_t)t->str_len)) {
            for (int i = 0; i < t->str_len; i++)
                emit("    U8 %d\n", (unsigned char)t->str[i]);
        } else {
            emit("    \"%.*s\"\n", t->str_len, t->str);
        }
        emit("    U8 0\n");
    }
}

bool codegen(Program *prog, Obj *obj, bool d32) {
    /* 可重定位输出：函数文本行 + 数据行 + bss + 导出符号；引用形态
     * 保留（@name/@hi:@lo/call label），布局由链接器完成 */
    cur_obj = obj;
    obj_d32 = d32;
    in_data = false;
    label_cnt = 0;

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
    /* 占位（仅声明/隐式声明，无函数体）不发射——否则 gen_stmts(NULL) 崩 */
    {
        int m = 0;
        for (int i = 0; i < n; i++)
            if (!flist[i]->is_decl)
                flist[m++] = flist[i];
        n = m;
    }
    if (n > 256) { fprintf(stderr, "codegen: too many functions\n"); exit(1); }
    for (int i = 0; i < n; i++)
        gen_func(flist[i]);

    /* 数据段放在代码之后（label 地址 = 代码后偏移，16 位寻址成立） */
    in_data = true;
    emit_data(prog);
    return true;
}
