/* symcc/src/link.h — 链接器接口（symld_link 核心，Task 6）
 *
 * 输入：N 个 .sym 可重定位对象（obj_read 产出或内存直接构造）+
 * crt0 模板路径（文本，作为首个 text 对象参与布局）。
 * 输出：绝对地址 asm（数据引用为数字；全局函数保留 label；末尾
 * halt: jmp halt）；out_bin 非 NULL → asm_assemble 转二进制写文件。
 * 任何错误 → 写 err->msg 返回 false。 */

#ifndef SYMCC_LINK_H
#define SYMCC_LINK_H

#include <stdbool.h>
#include <stdio.h>

#include "obj.h"

typedef struct LinkError { char msg[256]; } LinkError;

bool symld_link(Obj **objs, int n, const char *crt0_path,
                FILE *out_asm, FILE *out_bin, LinkError *err);

#endif /* SYMCC_LINK_H */
