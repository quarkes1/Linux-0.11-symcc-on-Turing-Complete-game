/* symcc/src/main.c — 编译器入口
 *
 * 用法: symcc.exe <file.c> [-o out.asm]
 * 流程: 读取源码 → tokenize → parse → codegen → 输出 .asm（文本）
 * 后续由游戏汇编器（或本地 emu/asm.exe）汇编为二进制。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long len;
    char *buf;
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        fprintf(stderr, "read error: %s\n", path);
        exit(1);
    }
    buf[len] = 0;
    fclose(fp);
    return buf;
}

int main(int argc, char **argv) {
    const char *in_file = NULL;
    const char *out_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else if (!in_file) {
            in_file = argv[i];
        } else {
            fprintf(stderr, "usage: symcc.exe <file.c> [-o out.asm]\n");
            return 2;
        }
    }
    if (!in_file) {
        fprintf(stderr, "usage: symcc.exe <file.c> [-o out.asm]\n");
        return 2;
    }

    char *src = read_file(in_file);
    FILE *out = out_file ? fopen(out_file, "w") : stdout;
    if (!out) {
        fprintf(stderr, "cannot open %s for writing\n", out_file);
        return 1;
    }
    bool ok = symcc_compile_text(src, out);
    if (out_file)
        fclose(out);
    return ok ? 0 : 1;
}
