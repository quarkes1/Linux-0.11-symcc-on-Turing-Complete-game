/* tests/test_typedef_enum.c — M2 Task 2 验收：typedef 与 enum */
typedef int myint;
enum Color { RED, GREEN = 5, BLUE };
enum Color c = BLUE;

int main(void) {
    myint x = 10;
    c = RED;
    return x + c + GREEN;   /* 10 + 0 + 5 = 15 */
}
