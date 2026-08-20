/* tests/test_fnptr.c — M2 Task 2 验收：函数指针（动态调用） */
int add(int a, int b) {
    return a + b;
}

int main(void) {
    int (*fp)(int, int) = add;
    return fp(3, 4);   /* 7 */
}
