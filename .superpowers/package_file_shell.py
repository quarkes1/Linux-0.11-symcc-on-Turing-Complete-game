from pathlib import Path
import hashlib,zipfile
root=Path.cwd()
log=(root/'.superpowers/file-shell-full-test.log').read_text(encoding='utf-8',errors='replace')
assert 'Ran 15 tests' in log and 'Ran 1 test' in log
assert log.count('stop=1 error=0')==8
assert 'nested=1 final_cwd=1' in log and 'edited=1 premature=0' in log
assert not any(x in log for x in ['FAILED (','Traceback (','make: ***'])
path=root/'docs/superpowers/2026-10-17-file-shell-update.md'
report=path.read_text(encoding='utf-8')
report+='\n## 完整验收\n\n`mingw32-make test` 退出码 0，包含八种 shell 客户机模式、Python CLI 15 项（损坏间接块测试含 6 个子情形）、MINIX 镜像测试及其余编译器/链接器/模拟器/M3 回归。独立审阅发现的间接块问题已复现、修复并复核通过。\n\n'
report+='- 真实 Shift+标点键事件生成重定向、heredoc 与 C 代码，客户机实际写入、读取并删除文件。\n'
report+='- 保存并重新启动读取新文件；追加、输入文件重定向、空 heredoc、兼容简写、隐藏文件递归删除通过。\n'
report+='- 错误语法不截断文件，失败 heredoc 不执行正文，过长行读到结束标记，重复 heredoc 不泄漏描述符或临时文件名。\n'
report+='- 完整日志保存在本机 `.superpowers/file-shell-full-test.log`。游戏内新版组合键与交互仍待用户复验，模拟器结果不作为游戏实测。\n\n'
report+='| 镜像 | 字节数 | SHA-256 |\n| --- | ---: | --- |\n'
for name in ['build/kernel.bin','build/rootfs.bin','build/m3/kernel.bin']:
 p=root/name
 report+=f'| `{name}` | {p.stat().st_size} | `{hashlib.sha256(p.read_bytes()).hexdigest()}` |\n'
path.write_text(report,encoding='utf-8')
with zipfile.ZipFile(root/'build/symphonyplus-interactive.zip','w',zipfile.ZIP_DEFLATED) as z:
 for name in ['build/kernel.bin','build/rootfs.bin','README.md']:
  z.write(root/name,Path(name).name)
 z.write(path,'verification.md')
with zipfile.ZipFile(root/'build/symphonyplus-interactive.zip') as z:
 assert z.testzip() is None
 for name in ['kernel.bin','rootfs.bin']:
  assert z.read(name)==(root/'build'/name).read_bytes()
part=log.split('tests/shell_test.exe --files\n',1)[1].split('tests/tick_test.exe',1)[0]
rows=[line for line in part.splitlines() if len(line)==96]
(root/'build/shell-screen.txt').write_text('\n'.join(rows[:40])+'\n',encoding='utf-8')
print(report.split('## 完整验收')[1])
