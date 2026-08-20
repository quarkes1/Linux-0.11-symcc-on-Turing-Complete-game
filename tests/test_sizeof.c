/* tests/test_sizeof.c — M2 Task 3 验收：sizeof 不生成求值代码 */
int main(void) {
    int a[10];
    char s[7];
    int x = sizeof a + sizeof(s) + sizeof(int) + sizeof(char) + sizeof a[0];
    return x;   /* 40 + 7 + 4 + 1 + 4 = 56 */
}
