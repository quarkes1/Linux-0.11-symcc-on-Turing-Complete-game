from pathlib import Path
import hashlib,zipfile
root=Path.cwd()
log=(root/'.superpowers/shell-ui-full-test.log').read_text(encoding='utf-8',errors='replace')
assert 'Ran 8 tests' in log and 'nested=1 final_cwd=1' in log and 'edited=1 premature=0' in log
assert 'boot=1 welcome=1 clear=1 cwd=1 pwd=1 long=1 dots=1 badflag=1' in log
assert log.count('stop=1 error=0')==6
path=root/'docs/superpowers/2026-10-17-shell-ui-update.md'
report=path.read_text(encoding='utf-8')
report+='\n## 完整回归结果与镜像\n\n`mingw32-make test` 退出码 0，六种 shell 客户机执行模式、Python CLI 8 项、MINIX 镜像 1 项及其余完整回归通过。独立代码审阅通过。完整日志保存在本机 `.superpowers/shell-ui-full-test.log`。\n\n'
report+='| 镜像 | 字节数 | SHA-256 |\n| --- | ---: | --- |\n'
for name in ['build/kernel.bin','build/rootfs.bin','build/m3/kernel.bin']:
 p=root/name
 report+=f'| `{name}` | {p.stat().st_size} | `{hashlib.sha256(p.read_bytes()).hexdigest()}` |\n'
report+='\n游戏复验：重新加载 RAM 与 DiskA 两个镜像，观察详细启动日志、Welcome 后清屏；输入 `ls -la /etc`、`cd /etc`、`pwd`、`sh`、`exit`，并在一条未提交的命令内按 Backspace，再按 Enter。\n'
path.write_text(report,encoding='utf-8')
with zipfile.ZipFile(root/'build/symphonyplus-interactive.zip','w',zipfile.ZIP_DEFLATED) as z:
 for name in ['build/kernel.bin','build/rootfs.bin','README.md']:
  z.write(root/name,Path(name).name)
 z.write(path,'verification.md')
part=log.split('tests/shell_test.exe --ui\n',1)[1].split('tests/shell_test.exe --keys',1)[0]
rows=[line for line in part.splitlines() if len(line)==96]
(root/'build/shell-screen.txt').write_text('\n'.join(rows[:40])+'\n',encoding='utf-8')
with zipfile.ZipFile(root/'build/symphonyplus-interactive.zip') as z:
 assert z.testzip() is None
 for name in ['kernel.bin','rootfs.bin']:
  assert z.read(name)==(root/'build'/name).read_bytes()
print(report.split('## 完整回归结果与镜像')[1])
