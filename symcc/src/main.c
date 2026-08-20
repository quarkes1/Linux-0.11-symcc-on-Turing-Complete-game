/* symcc/src/main.c — 编译器入口（M2 Task 7 gcc 式驱动）
 *
 * 用法: symcc.exe [选项] file1.c [file2.c ...]
 *   -E                只预处理，输出到 stdout 或 -o（.i 文本）
 *   -S                编译到可重定位 asm（.sym 文本对象，-o 或 默认 <file>.s）
 *   -c                编译到 .sym 对象（同 -S 内容；-o 或 默认 <file>.sym）
 *   -o <file>         输出文件（全链路默认 stdout 或 <file>.asm）
 *   -I <dir>          头文件搜索路径（可多次）
 *   -D <name[=val]>   预定义宏（可多次）
 *   -save-temps       保留中间文件（.i/.s/.sym 于当前目录）
 *   --d32             数据引用 32 位装载（链接输出不变，编译期开关）
 *   -v                打印各阶段文件名到 stderr
 * 无选项 = 全链路（每个文件 preprocess → compile obj → 统一 symld_link → 绝对 asm）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symcc.h"
#include "link.h"

#define MAX_FILES 256
#define MAX_OPT 64

static const char *usage_text =
    "用法: symcc.exe [选项] file1.c [file2.c ...]\n"
    "  -E                只预处理，输出到 stdout 或 -o（.i 文本）\n"
    "  -S                编译到可重定位 asm（.sym 文本对象，-o 或 默认 <file>.s）\n"
    "  -c                编译到 .sym 对象（同 -S 内容；-o 或 默认 <file>.sym）\n"
    "  -o <file>         输出文件（全链路默认 stdout 或 <file>.asm）\n"
    "  -I <dir>          头文件搜索路径（可多次）\n"
    "  -D <name[=val]>   预定义宏（可多次）\n"
    "  -save-temps       保留中间文件（.i/.s/.sym 于当前目录）\n"
    "  --d32             数据引用 32 位装载（链接输出不变，编译期开关）\n"
    "  -v                打印各阶段文件名到 stderr\n"
    "无选项 = 全链路（每个文件 preprocess → compile obj → 统一 symld_link → 绝对 asm）\n";

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long len;
    char *buf;
    if (!fp) {
        fprintf(stderr, "symcc: cannot open %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fprintf(stderr, "symcc: out of memory\n"); exit(1); }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        fprintf(stderr, "symcc: read error: %s\n", path);
        exit(1);
    }
    buf[len] = 0;
    fclose(fp);
    return buf;
}

/* 输出整块文本到文件（"-" 或 NULL = stdout） */
static void write_text(const char *path, const char *text) {
    if (path && strcmp(path, "-") != 0) {
        FILE *fp = fopen(path, "wb");
        if (!fp) { fprintf(stderr, "symcc: cannot write %s\n", path); exit(1); }
        fputs(text, fp);
        fclose(fp);
    } else {
        fputs(text, stdout);
    }
}

/* 输出对象到文件（"-" 或 NULL = stdout） */
static void write_obj(const char *path, const Obj *o) {
    FILE *fp;
    if (path && strcmp(path, "-") != 0) {
        fp = fopen(path, "wb");
        if (!fp) { fprintf(stderr, "symcc: cannot write %s\n", path); exit(1); }
    } else {
        fp = stdout;
    }
    if (!obj_write(o, fp))
        fprintf(stderr, "symcc: obj_write failed\n");
    if (fp != stdout)
        fclose(fp);
}

/* 派生输出路径：源文件同目录 + 换扩展名（gcc 惯例：-S foo/t.c → foo/t.s）。
 * buf 由调用方提供（≥1024 字节）。 */
static void derived_path(const char *path, const char *ext, char *buf, size_t n) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *p = slash > bslash ? slash : bslash;
    size_t dir_len = p ? (size_t)(p - path + 1) : 0;
    const char *base = p ? p + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t base_len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    if (dir_len + base_len + strlen(ext) + 1 >= n) {
        fprintf(stderr, "symcc: output path too long\n");
        exit(1);
    }
    memcpy(buf, path, dir_len);
    memcpy(buf + dir_len, base, base_len);
    strcpy(buf + dir_len + base_len, ext);
}

int main(int argc, char **argv) {
    enum { MODE_FULL, MODE_E, MODE_S, MODE_C } mode = MODE_FULL;
    const char *out_file = NULL;
    const char *inc_dirs[MAX_OPT], *defines[MAX_OPT];
    int n_inc = 0, n_def = 0;
    const char *files[MAX_FILES];
    int nf = 0;
    bool d32 = false, save_temps = false, verbose = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-E") == 0) mode = MODE_E;
        else if (strcmp(a, "-S") == 0) mode = MODE_S;
        else if (strcmp(a, "-c") == 0) mode = MODE_C;
        else if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "%s", usage_text); return 2; }
            out_file = argv[++i];
        } else if (strcmp(a, "-I") == 0 || strncmp(a, "-I", 2) == 0) {
            /* -I dir 与 gcc 连写 -Idir 都支持 */
            if (n_inc >= MAX_OPT) {
                fprintf(stderr, "%s", usage_text); return 2;
            }
            if (a[2])
                inc_dirs[n_inc++] = a + 2;
            else if (i + 1 < argc)
                inc_dirs[n_inc++] = argv[++i];
            else {
                fprintf(stderr, "%s", usage_text); return 2;
            }
        } else if (strcmp(a, "-D") == 0 || strncmp(a, "-D", 2) == 0) {
            /* -D name[=val] 与 gcc 连写 -Dname[=val] 都支持 */
            if (n_def >= MAX_OPT) {
                fprintf(stderr, "%s", usage_text); return 2;
            }
            if (a[2])
                defines[n_def++] = a + 2;
            else if (i + 1 < argc)
                defines[n_def++] = argv[++i];
            else {
                fprintf(stderr, "%s", usage_text); return 2;
            }
        } else if (strcmp(a, "--d32") == 0) d32 = true;
        else if (strcmp(a, "-save-temps") == 0) save_temps = true;
        else if (strcmp(a, "-v") == 0) verbose = true;
        else if (a[0] == '-') {
            fprintf(stderr, "symcc: unknown option %s\n", a);
            fprintf(stderr, "%s", usage_text);
            return 2;
        } else {
            if (nf >= MAX_FILES) {
                fprintf(stderr, "symcc: too many files\n"); return 2;
            }
            files[nf++] = a;
        }
    }
    if (nf == 0) {
        fprintf(stderr, "%s", usage_text);
        return 2;
    }
    /* 多文件 + 单输出（-o）+ 非全链路模式 → 冲突 */
    if (nf > 1 && mode != MODE_FULL && out_file) {
        fprintf(stderr, "symcc: -o with multiple files and -E/-S/-c\n");
        return 2;
    }

    if (mode == MODE_E) {
        for (int i = 0; i < nf; i++) {
            char *src = read_file(files[i]);
            char *out = preprocess_text(src, files[i], inc_dirs, n_inc,
                                        defines, n_def);
            const char *dest = out_file;
            char inferred[1100];
            if (!dest) {
                derived_path(files[i], ".i", inferred, sizeof inferred);
                dest = inferred;
            }
            if (verbose)
                fprintf(stderr, "symcc: -E %s -> %s\n", files[i],
                        dest ? dest : "(stdout)");
            write_text(dest, out);
            free(out);
            free(src);
        }
        return 0;
    }

    if (mode == MODE_S || mode == MODE_C) {
        const char *ext = mode == MODE_C ? ".sym" : ".s";
        for (int i = 0; i < nf; i++) {
            char *src = read_file(files[i]);
            Obj *obj = obj_new();
            if (!symcc_compile_obj(src, files[i], obj, d32,
                                   inc_dirs, n_inc, defines, n_def)) {
                fprintf(stderr, "symcc: compile failed: %s\n", files[i]);
                return 1;
            }
            const char *dest = out_file;
            char inferred[1100];
            if (!dest) {
                derived_path(files[i], ext, inferred, sizeof inferred);
                dest = inferred;
            }
            if (verbose)
                fprintf(stderr, "symcc: %s %s -> %s\n",
                        mode == MODE_C ? "-c" : "-S", files[i],
                        dest ? dest : "(stdout)");
            write_obj(dest, obj);
            obj_free(obj);
            free(src);
        }
        return 0;
    }

    /* 全链路：每个文件 → 对象 → 统一 symld_link → 绝对 asm */
    Obj *objs[MAX_FILES];
    for (int i = 0; i < nf; i++) {
        char *src = read_file(files[i]);
        Obj *obj = obj_new();
        if (!symcc_compile_obj(src, files[i], obj, d32,
                               inc_dirs, n_inc, defines, n_def)) {
            fprintf(stderr, "symcc: compile failed: %s\n", files[i]);
            return 1;
        }
        objs[i] = obj;
        if (save_temps) {
            char p[1100];
            derived_path(files[i], ".sym", p, sizeof p);
            write_obj(p, obj);
            if (verbose)
                fprintf(stderr, "symcc: save-temps %s -> %s\n", files[i], p);
        }
        free(src);
    }
    FILE *out = stdout;
    if (out_file) {
        out = fopen(out_file, "wb");
        if (!out) {
            fprintf(stderr, "symcc: cannot write %s\n", out_file);
            return 1;
        }
    }
    LinkError err;
    if (!symld_link(objs, nf, "runtime/crt0.asm", out, NULL, &err)) {
        fprintf(stderr, "symld: %s\n", err.msg);
        return 1;
    }
    if (verbose)
        fprintf(stderr, "symcc: link -> %s\n",
                out_file ? out_file : "(stdout)");
    if (out != stdout)
        fclose(out);
    for (int i = 0; i < nf; i++)
        obj_free(objs[i]);
    return 0;
}
