/* tests/test_char_literal.c — M2 Task 3 验收：字符字面量（转义） */
int main(void) {
    char c = 'A';
    int n = '\n';
    return c + (n == 10);   /* 65 + 1 = 66 */
}
