/* tests/test_udiv.c — 无符号除法（直接 div 指令）与 u 后缀 */
int main() {
    unsigned a = 4000000000u;
    unsigned b = 1000000u;
    return a / b == 4000;
}
