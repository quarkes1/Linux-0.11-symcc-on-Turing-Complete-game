/* tests/test_div.c — 有符号除法/取模（经运行时 __divsi3/__modsi3） */
int main() {
    int a = -7 / 2;      /* 向零截断 → -3 */
    int b = -7 % 2;      /* -1 */
    int c = 7 / -2;      /* -3 */
    int d = -7 / -2;     /* 3 */
    return (a == -3 && b == -1 && c == -3 && d == 3);
}
