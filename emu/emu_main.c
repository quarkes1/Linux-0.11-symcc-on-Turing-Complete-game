/* emu/emu_main.c — emu.exe 命令行入口
 *
 * 用法: emu.exe <in.bin> [ram_size]   （ram_size 默认 1MB）
 * 打印 exit_code；配合 asm.exe 使用：asm.exe x.asm x.bin && emu.exe x.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"

int main(int argc, char **argv) {
    FILE *fp;
    long len;
    uint8_t *bin;
    size_t ram_size = 1 << 20;
    EmuResult r;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: emu.exe <in.bin> [ram_size]\n");
        return 2;
    }
    if (argc == 3) {
        char *end;
        ram_size = (size_t)strtoul(argv[2], &end, 0);
        if (*end) {
            fprintf(stderr, "bad ram_size '%s'\n", argv[2]);
            return 2;
        }
    }
    fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 2; }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    bin = (uint8_t *)malloc((size_t)len ? (size_t)len : 1);
    if (!bin) { fclose(fp); return 2; }
    if (len > 0 && fread(bin, 1, (size_t)len, fp) != (size_t)len) {
        perror(argv[1]);
        fclose(fp);
        return 2;
    }
    fclose(fp);

    r = emu_run(bin, (size_t)len, ram_size, 1000000000ULL);
    if (r.error == 2)
        fprintf(stderr, "error: address out of bounds\n");
    else if (r.error == 1)
        fprintf(stderr, "error: instruction limit exceeded\n");
    printf("exit_code = %d\n", r.exit_code);
    free(bin);
    free(r.mem);
    free(r.disk);
    return r.error ? 1 : 0;
}
