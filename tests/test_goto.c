/* tests/test_goto.c — M2 Task 3 验收：goto 与标签（前向引用） */
int main(void) {
    int i = 0;
    int s = 0;
loop:
    i++;
    if (i > 5) goto done;
    s += i;
    goto loop;
done:
    return s;   /* 1+2+3+4+5 = 15 */
}
