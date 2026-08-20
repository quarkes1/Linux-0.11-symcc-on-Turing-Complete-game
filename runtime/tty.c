/* runtime/tty.c — 屏幕输出运行时（帧缓冲直写）
 *
 * M1 限制下的纯 C 子集写法：
 * - 无预处理器：帧缓冲基址/列数硬编码（0x2000/96，见 symcc/include/config.h，
 *   M2 预处理器就绪后改引）；
 * - 无数组下标：*(fb + i) 代替 fb[i]（char 指针算术不缩放）；
 * - 无类型转换：全局指针初始化 fb = 0x2000 代替 (char *)0x2000。
 * 光标移动：M1 只平移不滚屏（滚屏 M4 再做）。
 */

int cursor;          /* 0..(96*40)，全局默认 0 */
char *fb = 0x2000;   /* 帧缓冲基址 */

int putchar(int c) {
    if (c == 10) { cursor = cursor + 96 - cursor % 96; return 0; }   /* '\n' */
    if (c == 13) { cursor = cursor - cursor % 96; return 0; }        /* '\r' */
    if (c == 8 && cursor > 0) { cursor = cursor - 1; *(fb + cursor) = ' '; return 0; }  /* 退格 */
    *(fb + cursor) = c;
    cursor = cursor + 1;
    return 0;
}

int putstr(char *s) {
    while (*s) {
        putchar(*s);
        s = s + 1;      /* M1 无后缀 ++，显式推进 */
    }
    return 0;
}
