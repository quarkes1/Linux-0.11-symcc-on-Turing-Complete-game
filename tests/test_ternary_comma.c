/* tests/test_ternary_comma.c — M2 Task 3 验收：?: 与逗号表达式 */
int main(void) {
    int a = 1 ? 10 : 20;   /* 10 */
    int b = 0 ? 30 : 40;   /* 40 */
    int c = (a, b, 50);    /* 50 */
    return a + b + c;      /* 10 + 40 + 50 = 100 */
}
