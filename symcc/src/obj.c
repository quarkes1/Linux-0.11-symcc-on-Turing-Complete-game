/* symcc/src/obj.c — 可重定位对象模型实现（.sym 文本格式）
 *
 * obj_line_size/obj_label_name_len 移植自 codegen.c（原 line_size/
 * label_name_len，语义不变）：单行字节数与标签定义判定，供链接器
 * 布局时计算段内偏移（与游戏 .isa 伪指令多词展开逐字一致）。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj.h"

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* 行为 label 定义（`name:` 结尾、无指令缩进）则返回名字长度，否则 0。
 * 容忍前导换行（codegen 空行分隔标签；缩进规则不变——空格/制表不算） */
size_t obj_label_name_len(const char *line) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        n--;
    while (n > 0 && (line[0] == '\n' || line[0] == '\r')) {
        line++;
        n--;
    }
    if (n < 2 || line[n - 1] != ':')
        return 0;
    if (line[0] == ' ' || line[0] == ';' || line[0] == '"')
        return 0;
    for (size_t i = 0; i < n - 1; i++)
        if (!is_ident_char(line[i]))
            return 0;
    return n - 1;
}

/* 一行汇编的字节长度：label/注释/空行 = 0；指令 = 4；
 * 伪指令 push/pop/call/ret 是多词编码（8/8/20/12，游戏 .isa 与
 * asm.c 展开逐字一致）；U8/U16/U32 = 1/2/4；字符串行 = 内容字节数 */
long obj_line_size(const char *line) {
    const char *s = line;
    while (*s == ' ') s++;
    if (*s == ';' || *s == '\n' || *s == 0)
        return 0;
    if (obj_label_name_len(line))       /* label 定义行（`name:`，无缩进）：0 字节 */
        return 0;
    if (*s == '"') {
        const char *q = strchr(s + 1, '"');
        if (!q) {
            fprintf(stderr, "obj: 字符串数据行缺闭合引号: %s", line);
            exit(1);
        }
        return (long)(q - s - 1);
    }
    if (strncmp(s, "U8 ", 3) == 0) return 1;
    if (strncmp(s, "U16 ", 4) == 0) return 2;
    if (strncmp(s, "U32 ", 4) == 0) return 4;
    if (strncmp(s, "push ", 5) == 0) return 8;
    if (strncmp(s, "pop ", 4) == 0) return 8;
    if (strncmp(s, "call ", 5) == 0) return 20;
    if (strncmp(s, "ret", 3) == 0) return 12;
    return 4;
}

Obj *obj_new(void) {
    Obj *o = (Obj *)calloc(1, sizeof *o);
    if (!o) { fprintf(stderr, "out of memory\n"); exit(1); }
    return o;
}

static void free_lines(char **lines, int n) {
    for (int i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
}

void obj_free(Obj *o) {
    if (!o) return;
    free_lines(o->text_lines, o->n_text);
    free_lines(o->data_lines, o->n_data);
    for (ObjBss *b = o->bss; b; ) {
        ObjBss *nx = b->next;
        free(b->name);
        free(b);
        b = nx;
    }
    for (ObjSymbol *s = o->syms; s; ) {
        ObjSymbol *nx = s->next;
        free(s->name);
        free(s);
        s = nx;
    }
    free(o);
}

static void add_line(char ***lines, int *n, const char *line) {
    *lines = (char **)realloc(*lines, (size_t)(*n + 1) * sizeof **lines);
    if (!*lines) { fprintf(stderr, "out of memory\n"); exit(1); }
    (*lines)[*n] = (char *)malloc(strlen(line) + 1);
    if (!(*lines)[*n]) { fprintf(stderr, "out of memory\n"); exit(1); }
    strcpy((*lines)[*n], line);
    (*n)++;
}

void obj_add_text(Obj *o, const char *line) { add_line(&o->text_lines, &o->n_text, line); }
void obj_add_data(Obj *o, const char *line) { add_line(&o->data_lines, &o->n_data, line); }

void obj_add_symbol(Obj *o, const char *name, bool is_data, int segment,
                    long offset, long size) {
    ObjSymbol *s = (ObjSymbol *)calloc(1, sizeof *s);
    if (!s) { fprintf(stderr, "out of memory\n"); exit(1); }
    s->name = (char *)malloc(strlen(name) + 1);
    if (!s->name) { fprintf(stderr, "out of memory\n"); exit(1); }
    strcpy(s->name, name);
    s->len = (int)strlen(name);
    s->is_data = is_data;
    s->segment = segment;
    s->offset = offset;
    s->size = size;
    s->next = o->syms;
    o->syms = s;
}

void obj_add_bss(Obj *o, const char *name, long size) {
    ObjBss *b = (ObjBss *)calloc(1, sizeof *b);
    if (!b) { fprintf(stderr, "out of memory\n"); exit(1); }
    b->name = (char *)malloc(strlen(name) + 1);
    if (!b->name) { fprintf(stderr, "out of memory\n"); exit(1); }
    strcpy(b->name, name);
    b->size = size;
    b->next = o->bss;
    o->bss = b;
}

/* .sym 文本序列化 */
bool obj_write(const Obj *o, FILE *out) {
    fprintf(out, "; symcc object v1\n");
    fprintf(out, ".text\n");
    for (int i = 0; i < o->n_text; i++)
        fprintf(out, "%s\n", o->text_lines[i]);
    fprintf(out, ".data\n");
    for (int i = 0; i < o->n_data; i++)
        fprintf(out, "%s\n", o->data_lines[i]);
    for (ObjBss *b = o->bss; b; b = b->next)
        fprintf(out, ".bss %s %ld\n", b->name, b->size);
    for (ObjSymbol *s = o->syms; s; s = s->next)
        fprintf(out, ".sym %s %c %s %ld %ld\n", s->name,
                s->is_data ? 'D' : 'T',
                s->segment ? "data" : "text",
                s->offset, s->size);
    return true;
}

/* 反序列化：逐行读取（动态缓冲），识别 .text/.data/.bss/.sym 头行，
 * 注释/空行跳过；行尾换行去除 */
bool obj_read(Obj *o, FILE *in) {
    char *line = NULL;
    size_t cap = 0;
    int section = 0;   /* 0=头, 1=.text, 2=.data */
    for (;;) {
        size_t len = 0;
        int c;
        while ((c = fgetc(in)) != EOF) {
            if (len + 2 > cap) {
                cap = cap ? cap * 2 : 256;
                line = (char *)realloc(line, cap);
                if (!line) { fprintf(stderr, "out of memory\n"); exit(1); }
            }
            line[len++] = (char)c;
            if (c == '\n') break;
        }
        if (len == 0) break;   /* EOF */
        line[len] = 0;
        /* 去行尾换行 */
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = 0;
        if (line[0] == ';' || line[0] == 0)
            continue;
        if (strcmp(line, ".text") == 0) { section = 1; continue; }
        if (strcmp(line, ".data") == 0) { section = 2; continue; }
        if (strncmp(line, ".bss ", 5) == 0) {
            char name[512];
            long size;
            if (sscanf(line + 5, "%511s %ld", name, &size) == 2)
                obj_add_bss(o, name, size);
            continue;
        }
        if (strncmp(line, ".sym ", 5) == 0) {
            char name[512], kind[8], seg[16];
            long off, size;
            if (sscanf(line + 5, "%511s %7s %15s %ld %ld",
                       name, kind, seg, &off, &size) == 5)
                obj_add_symbol(o, name, kind[0] == 'D',
                               strcmp(seg, "data") == 0 ? 1 : 0, off, size);
            continue;
        }
        if (section == 1)
            obj_add_text(o, line);
        else if (section == 2)
            obj_add_data(o, line);
    }
    free(line);
    return true;
}
