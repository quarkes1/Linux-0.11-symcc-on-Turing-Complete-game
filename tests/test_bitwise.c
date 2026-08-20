/* tests/test_bitwise.c — M2 Task 3 验收：位运算（& | ^ ~ << >>） */
int main(void) {
    int a = 0xF0 & 0x3C;   /* 0x30 = 48 */
    int b = 0xF0 | 0x0F;   /* 0xFF = 255 */
    int c = 0xFF ^ 0x0F;   /* 0xF0 = 240 */
    int d = ~0x0F & 0xFF;  /* 0xF0 = 240 */
    int e = 1 << 4;        /* 16 */
    int f = 0xFF >> 4;     /* 15 */
    return a + b + c + d + e + f;   /* 814 */
}
