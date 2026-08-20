/* tests/test_array.c — M2 Task 2 验收：数组下标 */
int main(void) {
    int a[4];
    a[0] = 1;
    a[1] = 2;
    a[2] = 4;
    a[3] = 8;
    return a[0] + a[1] + a[2] + a[3];   /* 15 */
}
