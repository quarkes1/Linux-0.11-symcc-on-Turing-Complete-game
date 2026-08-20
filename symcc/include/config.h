/* symcc/include/config.h — 目标机（游戏沙盒）配置常量
 *
 * 由 Task 1（M0 验证）确定；尚未在游戏内复验时按默认值。
 * 说明：M1 无预处理器（#define 推迟到 M2），tty.c 中相关常量硬编码，
 *       crt0.asm 的 FRAMEBUF_BASE 占位符由 codegen 按本文件值替换。
 */
#ifndef SYMCC_CONFIG_H
#define SYMCC_CONFIG_H

#define FRAMEBUF_BASE 0x2000   /* 屏幕帧缓冲基址（ASCII 8 模式，96×40） */
#define COLS 96                /* 每行字符数 */
#define ROWS 40                /* 总行数 */

#endif /* SYMCC_CONFIG_H */
