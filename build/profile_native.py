from pathlib import Path
import re,struct
root=Path.cwd()
s=(root/'emu/emu.c').read_text(encoding='utf8')
s=s.replace('/* 与游戏 DiskA','extern void profile_pc(unsigned,const EmuResult *,unsigned long long);\n\n/* 与游戏 DiskA',1)
s=s.replace('if (opt->observe(&r, n - 1, opt->opaque))','profile_pc((unsigned)pc,&r,n-1);\n            if (opt->observe(&r, n - 1, opt->opaque))',1)
(root/'build/profile_emu.c').write_text(s,encoding='utf8')
common=['user/lib/crt0.sym','user/lib/syscall.sym']+['build/native/'+n+'.sym' for n in ['base','heap','stdio','format','string','convert','divsi3']]
def size(line):
    s=line.strip()
    if s.startswith('__sp_poll_L') and s.endswith(':'):return 60
    if not s or s.startswith(';') or s.endswith(':'):return 0
    op=s.split()[0]
    if op in ['je','jne','jb','jae','jbe','ja','jl','jge','jle','jg']:return 16
    if op=='call':return 32
    if op=='jmp':return 4 if re.match(r'jmp\s+(?:r\d+|zr|sp|flags)\b',s) else 16
    if op in ('push','pop'):return 8
    if op=='ret':return 12
    if op in ('U8','U16','U32','U64'):return int(op[1:])//8
    return 4
with (root/'build/profile.map').open('w') as out:
    for stage,parts in enumerate([['tokenize','preprocess','cpp_main'],['tokenize','parse','codegen','obj','cc1_main'],['obj','link_image','asm','isa','ld_main']]):
        pos=0x200000;name='start';start=pos
        for path in common+['build/native/'+n+'.sym' for n in parts]:
            section=''
            for line in (root/path).read_text(encoding='utf8').splitlines():
                if line.startswith('.'):
                    if line in ('.text','.data'):section=line
                    continue
                if section!='.text':continue
                if line.endswith(':') and not line.startswith('__sp_poll_L') and not re.match(r'^L(?:g)?\d+:$',line):
                    if pos>start:out.write(f'{stage} {start:x} {pos:x} {name}\n')
                    start=pos;name=Path(path).stem+':'+line[:-1]
                pos+=size(line)
        out.write(f'{stage} {start:x} {pos:x} {name}\n')
(root/'build/profile.keys').write_bytes(b'cd /tmp\ncat >test.c <<EOF\n#include <stdio.h>\nint main(void){printf("test\\n");return 0;}\nEOF\nsymcc test.c -o test && ./test\necho PROFILE DONE\n')
