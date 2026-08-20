/* tests/test_break_continue_while.c — M2 Task 3 验收：while/for 的 break/continue */
int main(void) {
    int s = 0;
    int i;
    for (i = 0; i < 10; i++) {
        if (i % 3 == 0) continue;
        s += i;
    }
    while (s > 30) s--;
    return s;   /* 1+2+4+5+7+8 = 27 */
}
