/* tests/test_sizeof_cast.c — M2 Task 2 验收：sizeof 与 cast */
int main(void) {
    int a[5];
    char c;
    c = (char)300;             /* 300 & 0xFF = 44 */
    return sizeof(a) + sizeof(int) + sizeof(char) + c;   /* 20+4+1+44 = 69 */
}
