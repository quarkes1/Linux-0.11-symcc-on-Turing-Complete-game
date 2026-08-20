/* tests/test_break_continue_do.c — M2 Task 3 验收：do-while + break/continue */
int main(void) {
    int i = 0;
    int s = 0;
    do {
        i++;
        if (i == 3) continue;
        if (i == 5) break;
        s += i;
    } while (i < 10);
    return s;   /* 1+2+4 = 7（跳 3，到 5 break） */
}
