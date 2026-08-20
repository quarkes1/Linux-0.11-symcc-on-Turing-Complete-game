/* tests/test_static_local.c — M2 Task 2 验收：static 局部（跨调用保持） */
int counter(void) {
    static int n;
    n = n + 1;
    return n;
}

int main(void) {
    int a = counter();   /* 1（求值顺序无关地取两次） */
    int b = counter();   /* 2 */
    return a + b * 10;   /* 1 + 20 = 21 */
}
