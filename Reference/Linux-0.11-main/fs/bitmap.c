
/* SYMPLUS-PORT: 4 inline-asm macros -> pure C (btsl/btrl/bsfl semantics, M3 runtime check) */

#define clear_block(addr) { \
	char *_p = (char *)(addr); int _i; \
	for (_i = 0; _i < 1024; _i++) \
		_p[_i] = 0; \
}

#define set_bit(nr,addr) (*(volatile int *)(addr) |= (1 << (nr)))
#define clear_bit(nr,addr) (*(volatile int *)(addr) &= ~(1 << (nr)))

/* statement expression unsupported -> static function (file-local only) */
static int find_first_zero(int *addr)
{
	int __res = 0;
	while (__res < 256 && addr[__res] == 0xffffffff)
		__res++;
	return __res;
}

