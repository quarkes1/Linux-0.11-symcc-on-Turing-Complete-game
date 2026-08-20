/* SYMPLUS-PORT: x86 端口 I/O → stub（SymphonyPlus 无端口；M3 映射到
 * 屏幕/磁盘地址空间时实现）。原 gcc 语句表达式 ({...}) 语法不支持。 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#define outb(value,port) ((void)(value), (void)(port))
#define inb(port) (0)
#define outb_p(value,port) ((void)(value), (void)(port))
#define inb_p(port) (0)

#endif
