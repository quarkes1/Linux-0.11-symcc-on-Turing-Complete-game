/* tests/test_control.c — Task 5 验收：控制流与短路逻辑 */
int main() {
    int i = 0;
    int sum = 0;
    while (i < 10) {
        sum = sum + i;
        i = i + 1;
    }
    if (sum == 45 && i == 10) {
        sum = sum * 2;
    } else {
        sum = 0;
    }
    for (i = 0; i < 5; i = i + 1) sum = sum + i;
    return sum;   /* 90 + 10 = 100 */
}
