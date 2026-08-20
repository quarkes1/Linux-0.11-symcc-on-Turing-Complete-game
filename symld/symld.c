/* symld/symld.c — 链接器 CLI
 * 用法: symld.exe file.sym [file.sym...] -o out.asm [--bin out.bin] [--crt0 path]
 *
 * 读取 .sym 可重定位对象 → symld_link（crt0 + 布局 + 重定位）→
 * 绝对地址 asm（可选 --bin 转二进制）。错误 → stderr + exit(1)。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc/src/obj.h"
#include "symcc/src/link.h"

static void usage(void) {
    fprintf(stderr,
            "用法: symld.exe file.sym [file.sym...] -o out.asm "
            "[--bin out.bin] [--crt0 path]\n");
}

int main(int argc, char **argv) {
    const char *out_asm = NULL, *out_bin = NULL, *crt0 = "runtime/crt0.asm";
    Obj *objs[128];
    int n = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_asm = argv[++i];
        } else if (strcmp(argv[i], "--bin") == 0 && i + 1 < argc) {
            out_bin = argv[++i];
        } else if (strcmp(argv[i], "--crt0") == 0 && i + 1 < argc) {
            crt0 = argv[++i];
        } else if (argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
            fprintf(stderr, "symld: unknown option %s\n", argv[i]);
            usage();
            return 2;
        } else {
            if (n >= 128) {
                fprintf(stderr, "symld: too many objects (max 128)\n");
                return 1;
            }
            FILE *f = fopen(argv[i], "rb");
            if (!f) {
                fprintf(stderr, "symld: cannot open %s\n", argv[i]);
                return 1;
            }
            Obj *o = obj_new();
            if (!obj_read(o, f)) {
                fprintf(stderr, "symld: cannot parse %s\n", argv[i]);
                fclose(f);
                return 1;
            }
            fclose(f);
            objs[n++] = o;
        }
    }

    if (n == 0 || !out_asm) {
        usage();
        return 2;
    }

    FILE *of = fopen(out_asm, "wb");
    if (!of) {
        fprintf(stderr, "symld: cannot write %s\n", out_asm);
        return 1;
    }
    FILE *bf = NULL;
    if (out_bin) {
        bf = fopen(out_bin, "wb");
        if (!bf) {
            fprintf(stderr, "symld: cannot write %s\n", out_bin);
            fclose(of);
            return 1;
        }
    }

    LinkError err;
    if (!symld_link(objs, n, crt0, of, bf, &err)) {
        fprintf(stderr, "symld: %s\n", err.msg);
        fclose(of);
        if (bf)
            fclose(bf);
        return 1;
    }
    fclose(of);
    if (bf)
        fclose(bf);
    return 0;
}
