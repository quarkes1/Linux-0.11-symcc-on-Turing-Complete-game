# scripts/fix_gcc_ext.py — 一次性：剩余 gcc 扩展（语句表达式宏 / asm 残留）-> 纯 C
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                      'Reference', 'Linux-0.11-main'))

def load(p):
    return open(p, 'rb').read().decode('latin-1').replace('\r\n', '\n')

def save(p, s):
    open(p, 'wb').write(s.encode('latin-1'))

def replace_range(p, start_prefix, end_suffix, new_lines):
    s = load(p)
    lines = s.split('\n')
    out = []
    in_drop = False
    replaced = 0
    for l in lines:
        if (not in_drop and l.startswith(start_prefix) and replaced == 0
                and 'SYMPLUS-PORT' not in l):
            in_drop = True
            out.extend(new_lines)
            replaced += 1
            if end_suffix in l:
                in_drop = False
            continue
        if in_drop:
            if end_suffix in l:
                in_drop = False
            continue
        out.append(l)
    assert not in_drop, p + ': unterminated at ' + start_prefix
    save(p, '\n'.join(out) + '\n')
    return replaced

def line_replace(p, old_prefix, new_line):
    s = load(p)
    lines = s.split('\n')
    n = 0
    for i, l in enumerate(lines):
        if l.startswith(old_prefix):
            lines[i] = new_line
            n += 1
    save(p, '\n'.join(lines) + '\n')
    return n

# ============ fs/exec.c：残留 set fs 汇编（幂等：已处理则跳过）============
p = 'fs/exec.c'
n = line_replace(p, '\t__asm__("pushl $0x17',
                 '\t/* SYMPLUS-PORT: set fs selector -> stub */')
assert n <= 1, 'exec.c fs'
print('exec.c __asm__ left:', load(p).count('__asm__'))

# ============ mm/memory.c：get_free_page 完整重写 ============
p = 'mm/memory.c'
n = replace_range(p, 'register unsigned long __res asm("ax");', ':"di","cx","dx");', [
    '\t/* SYMPLUS-PORT: std repne scasb free-page scan -> C loop (reverse scan) */',
    '\tint i;',
    '\tunsigned long __res = 0;',
    '\tfor (i = PAGING_PAGES - 1; i >= 0; i--) {',
    '\t\tif (!mem_map[i]) {',
    '\t\t\tmem_map[i] = 1;',
    '\t\t\t__res = LOW_MEM + (unsigned long)i * 4096;',
    '\t\t\tbreak;',
    '\t\t}',
    '\t}',
    '\treturn __res;',
])
assert n <= 1, 'get_free_page'
print('memory.c __asm__ left:', load(p).count('__asm__'),
      '| asm( left:', load(p).count('asm('))

# ============ init/main.c：CMOS_READ 语句表达式 -> 逗号表达式 ============
p = 'init/main.c'
n = replace_range(p, '#define CMOS_READ(addr)', '})', [
    '/* SYMPLUS-PORT: statement-expr -> comma expr (inb is stub in M2) */',
    '#define CMOS_READ(addr) ((outb_p(0x80|addr,0x70), inb_p(0x71)))',
])
assert n <= 1, 'init CMOS_READ'
print('init/main.c ({ left:', load(p).count('({'))
print('init/main.c __asm__ left:', load(p).count('__asm__'))

# ============ kernel/blk_drv/hd.c：CMOS_READ 语句表达式 ============
p = 'kernel/blk_drv/hd.c'
n = replace_range(p, '#define CMOS_READ(addr)', '})', [
    '/* SYMPLUS-PORT: statement-expr -> comma expr (inb is stub in M2) */',
    '#define CMOS_READ(addr) ((outb_p(0x80|addr,0x70), inb_p(0x71)))',
])
assert n <= 1, 'hd CMOS_READ'
print('hd.c ({ left:', load(p).count('({'))

# ============ include/unistd.h：_syscall0-3 int $0x80 -> stub ============
p = 'include/unistd.h'
for suffix, tag in [
    ('__NR_##name));', 'syscall0'),
    ('"b" ((long)(a)));', 'syscall1'),
    ('"c" ((long)(b)));', 'syscall2'),
    ('"d" ((long)(c)));', 'syscall3'),
]:
    n = replace_range(p, '__asm__ volatile ("int $0x80"', suffix, [
        '\t__res = -1; /* SYMPLUS-PORT: int $0x80 syscall -> stub (M3) */ \\',
    ])
    assert n <= 1, tag
print('unistd.h __asm__ left:', load(p).count('__asm__'))

# ============ kernel/vsprintf.c：do_div 语句表达式 -> 逗号表达式 ============
p = 'kernel/vsprintf.c'
n = replace_range(p, '#define do_div(n,base)', '})', [
    '/* SYMPLUS-PORT: statement-expr -> comma expr; n is modified in place */',
    '#define do_div(n,base) (((n) % (base)) + 0 * ((n) /= (base)))',
])
assert n <= 1, 'do_div'
print('vsprintf.c ({ left:', load(p).count('({'))
print('ALL DONE')
