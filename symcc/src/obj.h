/* symcc/src/obj.h — 可重定位对象模型（.sym 文本格式）
 *
 * 链接器（symld）消费：obj_read 产出直接进链接布局。
 * 引用形态（codegen 发射、链接器解析）：
 *   @name       = D16 数据引用（mov/load/store 的 imm）
 *   @hi:name/@lo:name = D32 数据引用（高位/低位半字）
 *   call name / jmp name / 条件跳转 name = J16 控制流引用
 * 段内标签：`name:` 行（无缩进）。符号 offset 由链接器布局时重算
 * （codegen 的 .sym 行 offset 传 0，链接器按标签行建表）。 */

#ifndef SYMCC_OBJ_H
#define SYMCC_OBJ_H

#include <stdbool.h>
#include <stdio.h>

typedef struct ObjSymbol {
    struct ObjSymbol *next;
    char *name;
    int len;
    bool is_data;      /* true=数据符号（变量）；false=函数符号 */
    int segment;       /* 0=text, 1=data（bss 符号 segment=1） */
    long offset;       /* 段内偏移（链接器布局时填入） */
    long size;
} ObjSymbol;

typedef struct ObjBss {
    struct ObjBss *next;
    char *name;
    long size;
} ObjBss;

typedef struct Obj {
    char **text_lines;  int n_text;    /* 可重定位 .text 行（不含表行） */
    char **data_lines;  int n_data;    /* .data 行 */
    ObjBss *bss;                       /* .bss 声明 */
    ObjSymbol *syms;                   /* 导出的全局符号（非 static） */
} Obj;

Obj *obj_new(void);
void obj_free(Obj *obj);

long obj_line_size(const char *line);        /* 单行字节数（含伪指令展开） */
size_t obj_label_name_len(const char *line); /* 行首 `name:` 的标签名长度（无 = 0） */

void obj_add_text(Obj *o, const char *line); /* 追加（行尾不含 \n，序列化时统一） */
void obj_add_data(Obj *o, const char *line);
void obj_add_symbol(Obj *o, const char *name, bool is_data, int segment,
                    long offset, long size);
void obj_add_bss(Obj *o, const char *name, long size);

bool obj_write(const Obj *o, FILE *out);     /* .sym 文本对象序列化 */
bool obj_read(Obj *o, FILE *in);             /* 反序列化（行缓冲动态增长） */

#endif /* SYMCC_OBJ_H */
