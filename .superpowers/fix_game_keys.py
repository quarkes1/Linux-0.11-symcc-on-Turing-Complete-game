from pathlib import Path
p=Path('Reference/Linux-0.11-main/kernel/symplus/process.c')
s=p.read_bytes()
s=s.replace(b'        if(key==13)c=10;',
 b'        /* SYMPLUS-PORT: game Backspace is 0x0d; Enter is 0x0a.\n'
 b'         * Do not apply terminal CR-to-LF conversion to hardware events. */\n'
 b'        if(key==13)c=8;')
s=s.replace(b'\r\n',b'\n')
p.write_bytes(s)
