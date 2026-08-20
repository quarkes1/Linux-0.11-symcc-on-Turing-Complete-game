/* SYMPLUS-PORT: gcc 内联汇编的 fs/gs 段读写 → 普通内存读写（无 MMU，
 * 用户段/内核段同地址空间，M3 引入保护后验证）。extern inline →
 * static（避免多文件重复定义）。 */
#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

static unsigned char get_fs_byte(const char *addr)
{
	return *addr;
}

static unsigned short get_fs_word(const unsigned short *addr)
{
	return *addr;
}

static unsigned long get_fs_long(const unsigned long *addr)
{
	return *addr;
}

static void put_fs_byte(char val, char *addr)
{
	*addr = val;
}

static void put_fs_word(short val, short *addr)
{
	*addr = val;
}

static void put_fs_long(unsigned long val, unsigned long *addr)
{
	*addr = val;
}

/* SYMPLUS-PORT: 段寄存器读写 → 常量（无段寄存器） */
static unsigned long get_fs(void)
{
	return 0;
}

static unsigned long get_ds(void)
{
	return 0;
}

static void set_fs(unsigned long val)
{
	(void)val;
}

#endif
