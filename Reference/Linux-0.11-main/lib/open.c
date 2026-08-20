/*
 *  linux/lib/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <stdarg.h>

int open(const char * filename, int flag, ...)
{
	register int res;
	va_list arg;

	va_start(arg,flag);
	/* SYMPLUS-PORT: int $0x80 syscall -> stub (M3 implements syscall ABI) */
	res = -1;
	if (res>=0)
		return res;
	errno = -res;
	return -1;
}
