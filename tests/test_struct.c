/* tests/test_struct.c — M2 Task 2 验收：struct 成员访问与赋值 */
struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point p;
    p.x = 3;
    p.y = 5;
    return p.x + p.y;   /* 8 */
}
