/* tests/test_init.c — M2 Task 2 验收：全局初始化器（标量/数组/字符串） */
int g = 42;
int arr[3] = { 1, 2, 3 };
char s[] = "hi";

int main(void) {
    return g + arr[2] + s[1];   /* 42 + 3 + 105 = 150 */
}
