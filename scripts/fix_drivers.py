# scripts/fix_drivers.py — 一次性：console.c / hd.c / floppy.c 内联汇编 -> 纯 C
# 行级处理（startswith 匹配 start 行、内容包含匹配 end 行），避免 heredoc 转义问题。
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                      'Reference', 'Linux-0.11-main'))

def load(p):
    return open(p, 'rb').read().decode('latin-1').replace('\r\n', '\n')

def save(p, s):
    open(p, 'wb').write(s.encode('latin-1'))

def replace_range(p, start_prefix, end_suffix, new_lines):
    """把第一处 start_prefix 行到（含）end_suffix 行替换为 new_lines。返回替换次数。
    只处理第一处匹配（其余由后续调用处理）。"""
    s = load(p)
    lines = s.split('\n')
    out = []
    in_drop = False
    replaced = 0
    for l in lines:
        if not in_drop and l.startswith(start_prefix) and replaced == 0:
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
    assert not in_drop, p + ': unterminated range at ' + start_prefix
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

def count_asm(p):
    return load(p).count('__asm__')

P = '/* SYMPLUS-PORT: %s */'

# ============ kernel/chr_drv/console.c ============
p = 'kernel/chr_drv/console.c'

# scrup EGA 内分支：搬 (lines-1) 行 + 擦最后一行
n = replace_range(p, '\t\t\t\t__asm__("cld', ':"cx","di","si");', [
    P % 'rep movsl+stosw scroll -> C loop',
    '\t\t\t\t{',
    '\t\t\t\t\tunsigned short *_d = (unsigned short *)video_mem_start;',
    '\t\t\t\t\tunsigned short *_s = (unsigned short *)origin;',
    '\t\t\t\t\tunsigned short *_e = (unsigned short *)(scr_end - video_size_row);',
    '\t\t\t\t\tint _i;',
    '\t\t\t\t\tfor (_i = 0; _i < (video_num_lines-1)*video_num_columns; _i++)',
    '\t\t\t\t\t\t_d[_i] = _s[_i];',
    '\t\t\t\t\tfor (_i = 0; _i < video_num_columns; _i++)',
    '\t\t\t\t\t\t_e[_i] = video_erase_char;',
    '\t\t\t\t}',
])
assert n == 1, 'scrup-ega-inner'

# scrup EGA else 分支：只擦最后一行
n = replace_range(p, '\t\t\t\t__asm__("cld', ':"cx","di");', [
    P % 'rep stosw erase -> C loop',
    '\t\t\t\t{',
    '\t\t\t\t\tunsigned short *_e = (unsigned short *)(scr_end - video_size_row);',
    '\t\t\t\t\tint _i;',
    '\t\t\t\t\tfor (_i = 0; _i < video_num_columns; _i++)',
    '\t\t\t\t\t\t_e[_i] = video_erase_char;',
    '\t\t\t\t}',
])
assert n == 1, 'scrup-ega-else'

# scrup 非 EGA：搬 (bottom-top-1) 行 + 擦 bottom-1 行
n = replace_range(p, '\t\t\t__asm__("cld', ':"cx","di","si");', [
    P % 'rep movsl+stosw scroll -> C loop',
    '\t\t{',
    '\t\t\tunsigned short *_d = (unsigned short *)(origin + video_size_row*top);',
    '\t\t\tunsigned short *_s = (unsigned short *)(origin + video_size_row*(top+1));',
    '\t\t\tunsigned short *_e = (unsigned short *)(origin + video_size_row*(bottom-1));',
    '\t\t\tint _i;',
    '\t\t\tfor (_i = 0; _i < (bottom-top-1)*video_num_columns; _i++)',
    '\t\t\t\t_d[_i] = _s[_i];',
    '\t\t\tfor (_i = 0; _i < video_num_columns; _i++)',
    '\t\t\t\t_e[_i] = video_erase_char;',
    '\t\t}',
])
assert n == 1, 'scrup-plain'

# scrup 非 EGA 分支（2 tab）：同 3-tab 块语义（搬 (bottom-top-1) 行 + 擦 bottom-1 行）
n = replace_range(p, '\t\t__asm__("cld', ':"cx","di","si");', [
    P % 'rep movsl+stosw scroll -> C loop',
    '\t\t{',
    '\t\t\tunsigned short *_d = (unsigned short *)(origin + video_size_row*top);',
    '\t\t\tunsigned short *_s = (unsigned short *)(origin + video_size_row*(top+1));',
    '\t\t\tunsigned short *_e = (unsigned short *)(origin + video_size_row*(bottom-1));',
    '\t\t\tint _i;',
    '\t\t\tfor (_i = 0; _i < (bottom-top-1)*video_num_columns; _i++)',
    '\t\t\t\t_d[_i] = _s[_i];',
    '\t\t\tfor (_i = 0; _i < video_num_columns; _i++)',
    '\t\t\t\t_e[_i] = video_erase_char;',
    '\t\t}',
])
assert n == 1, 'scrup-plain2'

# scrdown 两个分支（std 反向搬，等价正向语义：内容下移一行 + 擦 top 行）
for k in range(2):
    n = replace_range(p, '\t\t__asm__("std', ':"ax","cx","di","si");', [
        P % 'std rep movsl+stosw scroll-down -> C reverse loop',
        '\t\t{',
        '\t\t\tunsigned short *_d = (unsigned short *)(origin + video_size_row*(top+1));',
        '\t\t\tunsigned short *_s = (unsigned short *)(origin + video_size_row*top);',
        '\t\t\tunsigned short *_e = (unsigned short *)origin;',
        '\t\t\tint _i;',
        '\t\t\tfor (_i = (bottom-top-1)*video_num_columns - 1; _i >= 0; _i--)',
        '\t\t\t\t_d[_i] = _s[_i];',
        '\t\t\tfor (_i = 0; _i < video_num_columns; _i++)',
        '\t\t\t\t_e[_i] = video_erase_char;',
        '\t\t}',
    ])
    assert n == 1, 'scrdown-%d' % k

# csi_J / csi_K：register 绑定 -> 普通变量
n = line_replace(p, '\tlong count __asm__("cx");', '\tlong count;')
assert n == 2, 'csi count'
n = line_replace(p, '\tlong start __asm__("di");', '\tlong start;')
assert n == 2, 'csi start'

# csi_J / csi_K：stosw 擦除 -> C 循环（两处相同模式）
for k in range(2):
    n = replace_range(p, '\t__asm__("cld', ':"cx","di");', [
        P % 'rep stosw erase -> C loop',
        '\t{',
        '\t\tunsigned short *_p = (unsigned short *)start;',
        '\t\tlong _i;',
        '\t\tfor (_i = 0; _i < count; _i++)',
        '\t\t\t_p[_i] = video_erase_char;',
        '\t}',
    ])
    assert n == 1, 'csi-erase-%d' % k

# csi_m 写字符：movb _attr,%%ah ; movw -> 直接内存写
n = replace_range(p, '\t\t\t\t\t__asm__("movb _attr', ':"ax");', [
    P % 'attr+char store -> direct write',
    '\t\t\t\t\t*(unsigned short *)pos = (unsigned short)(((unsigned short)attr << 8) | (c & 0xff));',
])
assert n == 1, 'csi-m-putchar'

print('console.c __asm__ left:', count_asm(p))

# ============ kernel/blk_drv/hd.c ============
p = 'kernel/blk_drv/hd.c'
n = replace_range(p, '#define port_read(port,buf,nr)', ':"cx","di")', [
    P % 'insw -> stub (no hw in M2)',
    '#define port_read(port,buf,nr) ((void)(port), (void)(buf), (void)(nr))',
])
assert n == 1, 'port_read'
n = replace_range(p, '#define port_write(port,buf,nr)', ':"cx","si")', [
    P % 'outsw -> stub (no hw in M2)',
    '#define port_write(port,buf,nr) ((void)(port), (void)(buf), (void)(nr))',
])
assert n == 1, 'port_write'
n = line_replace(p, '\tregister int port asm("dx");', '\tregister int port;')
assert n == 1, 'hd port reg'
n = line_replace(p, '\t__asm__("divl %4":"=a" (block),"=d" (sec)',
                 '\tsec = block % hd_info[dev].sect; /* SYMPLUS-PORT: divl -> C div */')
assert n == 1, 'divl-1'
n = line_replace(p, '\t__asm__("divl %4":"=a" (cyl),"=d" (head)',
                 '\tcyl = block / hd_info[dev].head; head = block % hd_info[dev].head; /* SYMPLUS-PORT: divl -> C div */')
assert n == 1, 'divl-2'
print('hd.c __asm__ left:', count_asm(p))

# ============ kernel/blk_drv/floppy.c ============
p = 'kernel/blk_drv/floppy.c'
n = replace_range(p, '#define immoutb_p(val,port)', '"i" (port))', [
    P % 'outb+delay -> stub (no hw in M2)',
    '#define immoutb_p(val,port) ((void)(val), (void)(port))',
])
assert n == 1, 'immoutb_p'
n = replace_range(p, '#define copy_buffer(from,to)', ':"cx","di","si")', [
    P % 'rep movsl -> C dword loop',
    '#define copy_buffer(from,to) \\',
    '{ int _i; unsigned int *_f = (unsigned int *)(long)(from); \\',
    '  unsigned int *_t = (unsigned int *)(long)(to); \\',
    '  for (_i = 0; _i < BLOCK_SIZE/4; _i++) _t[_i] = _f[_i]; }',
])
assert n == 1, 'copy_buffer'
n = replace_range(p, ' \t__asm__("outb %%al,$12', 'DMA_WRITE)));', [
    P % 'DMA command outb -> stub',
])
assert n == 1, 'setup-DMA-outb'
n = line_replace(p, '\t\t__asm__("nop");', '\t\t/* SYMPLUS-PORT: nop delay -> stub */')
assert n == 1, 'floppy nop'
print('floppy.c __asm__ left:', count_asm(p))
print('ALL DONE')
