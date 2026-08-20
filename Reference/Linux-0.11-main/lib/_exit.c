/*
 *  linux/lib/_exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

volatile void _exit(int exit_code)
{
	/* SYMPLUS-PORT: int $0x80 syscall -> halt loop (real syscall in M3) */
	for (;;)
		;
}
