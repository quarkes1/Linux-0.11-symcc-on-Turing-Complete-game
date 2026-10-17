"""Generate auditable .sym ABI bridges from the fixed register layout."""
from pathlib import Path
from platform_config import read_config
root=Path(__file__).resolve().parents[1]
config=read_config()
lines=['.text']
exports=[]
def label(name): lines.append(name+':');exports.append(name)
def emit(text): lines.extend('    '+x for x in text.splitlines())
def const(reg,val): emit(f'mov {reg}, {val>>16}\nlsl {reg}, {reg}, 16\nor {reg}, {reg}, {val&65535}')
label('sp_syscall_entry')
emit('push flags')
for r in range(1,14): emit(f'push r{r}')
emit('mov r1, sp');const('r2',config['SP_CONTEXT_ADDR'])
for r in range(1,14):
    emit(f'add r3, r1, {(13-r)*4}\nload_32 r4, [r3]\nadd r3, r2, {r*4}\nstore_32 [r3], r4')
emit('add r3, r1, 52\nload_32 r4, [r3]\nadd r3, r2, 60\nstore_32 [r3], r4')
emit('add r4, r1, 60\nadd r3, r2, 56\nstore_32 [r3], r4')
emit('add r3, r1, 56\nload_32 r4, [r3]\nadd r3, r2, 64\nstore_32 [r3], r4')
const('sp',config['SP_KERNEL_STACK']);emit('push r2\ncall sp_dispatch\nadd sp, sp, 4\njmp sp_restore')
label('sp_resume');emit('add r1, sp, 4\nload_32 r1, [r1]')
lines.append('sp_restore:');emit('mov flags, r1\nadd r13, flags, 56\nload_32 sp, [r13]\nadd r13, flags, 64\nload_32 r13, [r13]\npush r13')
for r in range(1,14): emit(f'add r13, flags, {r*4}\nload_32 r{r}, [r13]')
emit('ret')
for name,insn in [('sp_keyboard_event','keyboard'),('sp_time_low','time_0'),('sp_time_high','time_1')]:
    label(name);emit(f'{insn} r1\nret')
label('sp_disk_read32');emit('add r9, sp, 4\nload_32 r2, [r9]\npload r1, [r2]\nret')
label('sp_disk_write32');emit('add r9, sp, 4\nload_32 r2, [r9]\nadd r9, sp, 8\nload_32 r1, [r9]\npstore [r2], r1\nret')
label('sp_copy');emit('add r9, sp, 4\nload_32 r1, [r9]\nadd r9, sp, 8\nload_32 r2, [r9]\nadd r9, sp, 12\nload_32 r3, [r9]')
lines.append('sp_copy_loop:');emit('cmp r3, 32\njb sp_copy_tail')
for _ in range(8):emit('load_32 r4, [r2]\nstore_32 [r1], r4\nadd r1, r1, 4\nadd r2, r2, 4')
emit('sub r3, r3, 32\njmp sp_copy_loop')
lines.append('sp_copy_tail:');emit('cmp r3, 0\nje sp_copy_done\nload_32 r4, [r2]\nstore_32 [r1], r4\nadd r1, r1, 4\nadd r2, r2, 4\nsub r3, r3, 4\njmp sp_copy_tail')
lines.append('sp_copy_done:');emit('ret')
label('sp_zero');emit('add r9, sp, 4\nload_32 r1, [r9]\nadd r9, sp, 8\nload_32 r2, [r9]')
lines.append('sp_zero_loop:');emit('cmp r2, 32\njb sp_zero_tail')
for _ in range(8):emit('store_32 [r1], zr\nadd r1, r1, 4')
emit('sub r2, r2, 32\njmp sp_zero_loop')
lines.append('sp_zero_tail:');emit('cmp r2, 0\nje sp_zero_done\nstore_32 [r1], zr\nadd r1, r1, 4\nsub r2, r2, 4\njmp sp_zero_tail')
lines.append('sp_zero_done:');emit('ret')
lines+=['.sym '+name+' T text 0 0' for name in exports]
(root/'runtime/platform.sym').write_text('\n'.join(lines)+'\n')
user=root/'user/lib';user.mkdir(parents=True,exist_ok=True)
user.joinpath('syscall.sym').write_text('''.text
sp_call:
    add r9, sp, 4
    load_32 r1, [r9]
    add r9, sp, 8
    load_32 r2, [r9]
    add r9, sp, 12
    load_32 r3, [r9]
    add r9, sp, 16
    load_32 r4, [r9]
    jmp 0x104
.sym sp_call T text 0 0
''')
user.joinpath('crt0.sym').write_text('''.text
_start:
    call main
    push r1
    call _exit
user_hang:
    jmp user_hang
.sym _start T text 0 0
''')
probe=['.text','sp_reg_probe:','    push r10','    push r11','    push r12']
for r in range(2,13):probe.append(f'    mov r{r}, {r*101}')
probe+=['    mov r1,20','    counter flags','    add flags,flags,32','    push flags','    jmp 0x104',
        '    cmp r1,0','    jle reg_probe_fail']
for r in range(2,13):probe += [f'    cmp r{r},{r*101}','    jne reg_probe_fail']
probe+=['    mov r1,0','    jmp reg_probe_done','reg_probe_fail:','    mov r1,1','reg_probe_done:',
        '    pop r12','    pop r11','    pop r10','    ret','.sym sp_reg_probe T text 0 0']
with user.joinpath('syscall.sym').open('a') as out:out.write('\n'.join(probe)+'\n')
