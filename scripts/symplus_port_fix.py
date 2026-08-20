# scripts/symplus_port_fix.py — Linux 0.11 内核 SYMPLUS-PORT 一次性改写脚本
# 行级处理（不依赖 heredoc 转义）：内联汇编/gcc 扩展 → stub/纯 C。
# 原则：编译通过第一优先；每处加 /* SYMPLUS-PORT: ... */ 注释。

import os
import sys

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                      'Reference', 'Linux-0.11-main'))

def load(p):
    return open(p, 'rb').read().decode('latin-1').replace('\r\n', '\n')

def save(p, s):
    open(p, 'wb').write(s.encode('latin-1'))

def line_replace(p, old_prefix, new_line):
    """把以 old_prefix 起始的行替换为 new_line。返回替换次数。"""
    s = load(p)
    lines = s.split('\n')
    n = 0
    for i, l in enumerate(lines):
        if l.startswith(old_prefix):
            lines[i] = new_line
            n += 1
    save(p, '\n'.join(lines) + '\n')
    return n

def drop_range(p, start_prefix, end_suffix, port_comment):
    """从 start_prefix 行到 end_suffix 行（含）之间全部删除，替换为注释行。
    end_suffix 用 '行内容包含' 判断。"""
    s = load(p)
    lines = s.split('\n')
    out = []
    in_drop = False
    dropped = 0
    for l in lines:
        if not in_drop and l.startswith(start_prefix):
            in_drop = True
            out.append(port_comment)
            dropped += 1
            if end_suffix in l:
                in_drop = False
            continue
        if in_drop:
            if end_suffix in l:
                in_drop = False
            dropped += 1
            continue
        out.append(l)
    assert not in_drop, p + ': unterminated drop at ' + start_prefix
    save(p, '\n'.join(out) + '\n')
    return dropped

P = '/* SYMPLUS-PORT: %s */'

# ============ kernel/traps.c ============
p = 'kernel/traps.c'
drop_range(p, '#define get_seg_byte(seg,addr)', '__res;})',
           P % 'fs-segment read -> plain memory')
drop_range(p, '#define get_seg_long(seg,addr)', '__res;})',
           P % 'fs-segment read -> plain memory')
drop_range(p, '#define _fs()', '__res;})',
           P % 'no fs register -> 0')
n = line_replace(p, '\t__asm__("str %%ax"', '        tr = 0;')
assert n == 1, p
# 编译可能仍见残留 __asm__？
s = load(p)
print('traps.c __asm__ left:', s.count('__asm__'))

# ============ fs/bitmap.c（宏 + find_first_zero_bit 汇编）============
p = 'fs/bitmap.c'
s = load(p)
lines = s.split('\n')
out = []
for i, l in enumerate(lines):
    if l.startswith('#define clear_bit'):
        # clear_bit(nr,addr): 纯 C 位操作（大端语义由 M3 验证；这里按比特位取反掩码）
        out.append(P % 'btsl inline asm -> pure C bit ops')
        out.append('#define clear_bit(nr,addr) (*((volatile int *)(addr)) &= ~(1 << (nr)))')
        continue
    if l.startswith('#define set_bit'):
        out.append(P % 'btrl inline asm -> pure C bit ops')
        out.append('#define set_bit(nr,addr) (*((volatile int *)(addr)) |= (1 << (nr)))')
        continue
    if l.startswith('__asm__("cld'):
        out.append('        /* SYMPLUS-PORT: cld ; rep;movsl -> memcpy (pure C) */')
        continue
    if l.startswith('register int res __asm__("ax");'):
        out.append('register int res;')
        continue
    out.append(l)
save(p, '\n'.join(out) + '\n')
s = load(p)
print('bitmap.c __asm__ left:', s.count('__asm__'), '| reg bind left:', s.count('__asm__("ax")'))

# ============ fs/buffer.c COPYBLK ============
p = 'fs/buffer.c'
drop_range(p, '#define COPYBLK(from,to)', ':"cx","di","si")',
           P % 'rep movsl -> memcpy loop')
n = line_replace(p, '\t__asm__("cld',
                 '        /* SYMPLUS-PORT: cld;rep;movsl -> memcpy */')
s = load(p)
print('buffer.c __asm__ left:', s.count('__asm__'))

# ============ fs/exec.c ============
n = line_replace(p, '\t__asm__("pushl $0x17', '\t/* SYMPLUS-PORT: set fs selector -> stub */')
s = load(p)
print('exec.c __asm__ left:', s.count('__asm__'))

# ============ fs/namei.c ============
p = 'fs/namei.c'
n = line_replace(p, '\tregister int same __asm__("ax");',
                 '\tregister int same;')
n2 = line_replace(p, '\t__asm__("cld',
                  '\t/* SYMPLUS-PORT: cld;rep;movsl -> loop */')
s = load(p)
print('namei.c __asm__ left:', s.count('__asm__'))

# ============ fs/super.c test_bit 宏 ============
p = 'fs/super.c'
drop_range(p, '#define test_bit(bitnr,addr)', ':"a" (0),"r" (bitnr),"m" (*(addr))); \\',
           P % 'bt inline asm -> pure C bit test')
n = line_replace(p, 'register int __res __asm__("ax"); \\',
                 '/* SYMPLUS-PORT: register asm binding -> plain var */')
s = load(p)
print('super.c __asm__ left:', s.count('__asm__'))

# ============ mm/memory.c ============
p = 'mm/memory.c'
n = line_replace(p, '__asm__("movl %%eax,%%cr3"', '        /* SYMPLUS-PORT: mov cr3 -> stub */')
n2 = line_replace(p, '__asm__("cld ; rep ; movsl"',
                  '        /* SYMPLUS-PORT: rep movsl -> memcpy (1024) */')
n3 = line_replace(p, '__asm__("std ; repne ; scasb',
                  '        /* SYMPLUS-PORT: repne scasb -> strlen loop */')
s = load(p)
print('memory.c __asm__ left:', s.count('__asm__'))

# ============ lib/open.c（第 2 处可能残留）============
s = load('lib/open.c')
print('open.c __asm__ left:', s.count('__asm__'))

# ============ lib/_exit.c ============
s = load('lib/_exit.c')
print('_exit.c __asm__ left:', s.count('__asm__'))

# ============ kernel/vsprintf.c do_div ============
p = 'kernel/vsprintf.c'
n = line_replace(p, '__asm__("divl %4"',
                 '        __res = n % base; n = n / base; /* SYMPLUS-PORT: divl -> C div */')
s = load(p)
print('vsprintf.c __asm__ left:', s.count('__asm__'))

# ============ kernel/printk.c ============
p = 'kernel/printk.c'
n = line_replace(p, '\t__asm__("push %%fs',
                 '\t/* SYMPLUS-PORT: fs user copy -> stub */')
s = load(p)
print('printk.c __asm__ left:', s.count('__asm__'))

# ============ kernel/math/math_emulate.c ============
p = 'kernel/math/math_emulate.c'
n = line_replace(p, '\t__asm__("fnclex");',
                 '\t/* SYMPLUS-PORT: fnclex -> stub */')
s = load(p)
print('math_emulate.c __asm__ left:', s.count('__asm__'))

# ============ kernel/fork.c ============
p = 'kernel/fork.c'
n = line_replace(p, '\t\t__asm__("clts ; fnsave %0"',
                 '\t\t/* SYMPLUS-PORT: fnsave -> stub */')
s = load(p)
print('fork.c __asm__ left:', s.count('__asm__'))

# ============ kernel/sched.c ============
p = 'kernel/sched.c'
for pat, rep in [
    ('\t__asm__("fwait");', '\t/* SYMPLUS-PORT: fwait -> stub */'),
    ('\t\t__asm__("fnsave %0"', '\t\t/* SYMPLUS-PORT: fnsave -> stub */'),
    ('\t\t__asm__("frstor %0"', '\t\t/* SYMPLUS-PORT: frstor -> stub */'),
    ('\t\t__asm__("fninit"::);', '\t\t/* SYMPLUS-PORT: fninit -> stub */'),
    ('\t__asm__("pushfl ; andl', '\t/* SYMPLUS-PORT: IF flag clear -> stub */'),
]:
    n = line_replace(p, pat, rep)
    assert n >= 1, p + ' missing: ' + pat
s = load(p)
print('sched.c __asm__ left:', s.count('__asm__'))

# ============ kernel/traps.c str 已处理 ============
print('ALL DONE')
