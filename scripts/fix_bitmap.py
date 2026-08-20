# scripts/fix_bitmap.py — 一次性：bitmap.c 宏区（行 13-45）替换为纯 C 版本
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                      'Reference', 'Linux-0.11-main'))

p = 'fs/bitmap.c'
s = open(p, 'rb').read().decode('latin-1').replace('\r\n', '\n')
lines = s.split('\n')

# 行 13-45（1-based, 0-based 12..44）是宏区；行 46 为空行，47 起为 free_block
out = lines[:12]
out += [
'/* SYMPLUS-PORT: 4 inline-asm macros -> pure C (btsl/btrl/bsfl semantics, M3 runtime check) */',
'',
'#define clear_block(addr) { \\',
'\tchar *_p = (char *)(addr); int _i; \\',
'\tfor (_i = 0; _i < 1024; _i++) \\',
'\t\t_p[_i] = 0; \\',
'}',
'',
'#define set_bit(nr,addr) (*(volatile int *)(addr) |= (1 << (nr)))',
'#define clear_bit(nr,addr) (*(volatile int *)(addr) &= ~(1 << (nr)))',
'',
'/* statement expression unsupported -> static function (file-local only) */',
'static int find_first_zero(int *addr)',
'{',
'\tint __res = 0;',
'\twhile (__res < 256 && addr[__res] == 0xffffffff)',
'\t\t__res++;',
'\treturn __res;',
'}',
'',
]
out += lines[46:]

open(p, 'wb').write(('\n'.join(out) + '\n').encode('latin-1'))
s = open(p, 'rb').read().decode('latin-1')
print('bitmap.c __asm__ left:', s.count('__asm__'), '| total lines:', len(out))
