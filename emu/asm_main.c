/* emu/asm_main.c — asm.exe 命令行入口
 *
 * 用法: asm.exe <in.asm> <out.bin>
 * 输出为大端字节流（与游戏内汇编器一致，RAM/DiskA 已配置 Big endian）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/asm.h"

int main(int argc, char **argv) {
    FILE *fp;
    long len;
    char *text;
    uint8_t *out;
    size_t cap;
    AsmError err;
    int n;

    if (argc != 3) {
        fprintf(stderr, "usage: asm.exe <in.asm> <out.bin>\n");
        return 2;
    }
    fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 2; }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    text = (char *)malloc((size_t)len + 1);
    if (!text) { fclose(fp); return 2; }
    if (len > 0 && fread(text, 1, (size_t)len, fp) != (size_t)len) {
        perror(argv[1]);
        fclose(fp);
        return 2;
    }
    text[len] = 0;
    fclose(fp);

    cap = (size_t)len * 16 + 65536;     /* call=5 词展开，宽松上限 */
    out = (uint8_t *)malloc(cap);
    if (!out) return 2;

    n = asm_assemble(text, out, cap, &err);
    if (n < 0) {
        fprintf(stderr, "%s:%d: %s\n", argv[1], err.line, err.msg);
        return 1;
    }
    fp = fopen(argv[2], "wb");
    if (!fp) { perror(argv[2]); return 2; }
    if (n > 0 && fwrite(out, 1, (size_t)n, fp) != (size_t)n) {
        perror(argv[2]);
        fclose(fp);
        return 2;
    }
    fclose(fp);
    printf("assembled %d bytes -> %s\n", n, argv[2]);
    return 0;
}
