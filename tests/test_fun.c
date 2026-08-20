/* tests/test_fun.c — Task 6 验收：函数调用、递归、全局变量、void */
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}

int g;                 /* 全局变量 */

void setg(int v) {
    g = v;
}

int main() {
    setg(40);
    g = g + 2;
    return fact(5) + g;   /* 120 + 42 = 162 */
}
