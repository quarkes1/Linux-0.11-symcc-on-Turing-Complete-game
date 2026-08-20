/* symcc/src/main.c — 编译器入口
 *
 * 用法: symcc.exe file1.c [file2.c ...] [-o out.asm]
 * 流程: 读取全部源文件（按命令行顺序拼接）→ tokenize → parse →
 *       codegen → 输出 .asm（文本）
 * 后续由游戏汇编器（或本地 emu/asm.exe）汇编为二进制。
 * 多文件：函数已由预扫描统一注册（见 parse.c pre_scan_functions），
 * 因此被调函数可写在调用方之后（如 runtime/divsi3.c 放最后）。
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
    const char *out_file = NULL;
    const char *files[64];
    int nf = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else {
            files[nf++] = argv[i];
        }
    }
    if (nf == 0) {
        fprintf(stderr, "usage: symcc.exe <file.c> [more.c...] [-o out.asm]\n");
        return 2;
    }

    /* 拼接全部源文件（顺序 = 命令行顺序；多文件可合并全局符号） */
    char *src = NULL;
    size_t total = 0;
    for (int i = 0; i < nf; i++) {
        char *buf = read_file(files[i]);
        size_t len = strlen(buf);
        char *newsrc = (char *)realloc(src, total + len + 2);
        if (!newsrc) { fprintf(stderr, "out of memory\n"); exit(1); }
        src = newsrc;
        memcpy(src + total, buf, len);
        total += len;
        src[total++] = '\n';          /* 文件间加换行，避免行尾粘连 */
        src[total] = 0;
        free(buf);
    }

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
