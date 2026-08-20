/* tests/test_switch.c — M2 Task 3 验收：switch 比较链 + fallthrough */
int main(void) {
    int x = 2;
    int r = 0;
    switch (x) {
    case 1: r = 10; break;
    case 2: r = 20; break;
    case 3: r = 30; break;
    }
    switch (x) {
    case 1: r += 1;
    case 2: r += 2;
    case 3: r += 3; break;
    default: r = 0;
    }
    return r;   /* 20 + (2+3) = 25 */
}
