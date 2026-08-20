# scripts/fix_unistd.py — 一次性：unistd.h _syscall0-3 int $0x80 -> stub
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                      'Reference', 'Linux-0.11-main'))

def load(p):
    return open(p, 'rb').read().decode('latin-1').replace('\r\n', '\n')

p = 'include/unistd.h'
s = load(p)
lines = s.split('\n')
out = []
n = 0
in_drop = False
for l in lines:
    if l.startswith('__asm__ volatile ("int $0x80"') and not in_drop:
        in_drop = True
        n += 1
        out.append('\t__res = -1; /* SYMPLUS-PORT: int $0x80 syscall -> stub (M3) */ \\')
        continue
    if in_drop:
        if l.startswith('\t: "') or l.startswith(':"'):
            # 约束行（以 : 开头）
            if '));' in l:
                in_drop = False
            continue
        in_drop = False
        out.append(l)
        continue
    out.append(l)
assert not in_drop, 'unterminated'
open(p, 'wb').write(('\n'.join(out) + '\n').encode('latin-1'))
print('unistd.h __asm__ left:', load(p).count('__asm__'), '| replaced:', n)
