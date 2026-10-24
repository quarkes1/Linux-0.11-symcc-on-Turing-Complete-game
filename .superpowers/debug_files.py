import sys
sys.path.insert(0,'tests')
from test_emu_cli import CliTest
t=CliTest()
mod,disk,screen=t.file_session('mkdir /tmp/work\ncat >/tmp/work/note <<EOF\nalpha\nbeta\nEOF\ncat /tmp/work/note\n')
print(screen.decode(errors='replace'))
print(repr(mod.read_file(disk,'/tmp/work/note')))
