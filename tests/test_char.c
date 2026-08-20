/* tests/test_char.c — Task 7 验收：char 存储与符号扩展 */
int main() {
    char c = 'A';
    char *p = &c;
    *p = *p + 1;
    return c;            /* 'B' = 66 */
}
