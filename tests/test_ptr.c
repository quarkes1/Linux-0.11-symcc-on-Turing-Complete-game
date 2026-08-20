/* tests/test_ptr.c — Task 7 验收：取址/解引用/指针算术 */
int main() {
    int x = 10;
    int *p = &x;
    *p = *p + 5;
    return x;            /* 15 */
}
