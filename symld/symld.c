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
    LinkOptions options={0,0xe0000,0x100,0x104,SP_IMAGE_LEGACY};
    Obj *objs[128];
    int n = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            const char *kind=argv[++i];
            if(!strcmp(kind,"kernel"))options.image_kind=SP_IMAGE_KERNEL;
            else if(!strcmp(kind,"user")) {
                options.image_kind=SP_IMAGE_USER;options.base=0x200000;options.limit=0x270000;
            } else {usage();return 2;}
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
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
    if (!symld_link_ex(objs, n, crt0, &options, of, bf, &err)) {
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
