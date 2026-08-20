/* tests/test_bitfield.c — M2 Task 2 验收：位域（大端，最高位起打包） */
struct Flags {
    unsigned a : 4;
    unsigned b : 4;
    unsigned c : 8;
};

int main(void) {
    struct Flags f;
    f.a = 7;
    f.b = 2;
    f.c = 64;
    return f.a * 10 + f.b;   /* 70 + 2 = 72 */
}
