/* runtime/divsi3.c — 有符号除法/取模运行时
 *
 * 硬件 div/mod 为无符号（模拟器与 ISA 一致），有符号除/模由这两个函数
 * 完成：取绝对值做无符号除，再按符号修正（向零截断，符合 C 语义）。
 * 约定：全栈传参（arg0 = 被除数、arg1 = 除数），结果在 r1。
 * 注：a == INT_MIN 时取绝对值溢出，为 UB（教学可接受）。
 */
int __divsi3(int a, int b) {
    unsigned ua;
    unsigned ub;
    unsigned q;
    ua = a;
    ub = b;
    if (a < 0)
        ua = 0 - ua;
    if (b < 0)
        ub = 0 - ub;
    q = ua / ub;
    if ((a < 0) != (b < 0))
        return 0 - q;
    return q;
}

int __modsi3(int a, int b) {
    unsigned ua;
    unsigned ub;
    unsigned r;
    ua = a;
    ub = b;
    if (a < 0)
        ua = 0 - ua;
    if (b < 0)
        ub = 0 - ub;
    r = ua % ub;
    if (a < 0)
        return 0 - r;
    return r;
}
