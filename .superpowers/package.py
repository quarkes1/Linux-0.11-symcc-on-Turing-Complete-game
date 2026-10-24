from pathlib import Path
import hashlib,re,zipfile
root=Path.cwd();log=(root/'.superpowers/full-test.log').read_text(encoding='utf-8',errors='replace')
assert 'Ran 7 tests' in log and 'REG PASS' in log and 'PROC PASS' in log and 'EXEC PASS' in log
names=['build/kernel.bin','build/rootfs.bin','build/m3/kernel.bin']
rows=[]
for name in names:
    p=root/name;rows.append(f'| `{name}` | {p.stat().st_size} | `{hashlib.sha256(p.read_bytes()).hexdigest()}` |')
count=len(re.findall(r'^PASS tests/test_',log,re.M))
report='''# SymphonyPlus 交互系统验收记录

日期：2026-10-17。代码位于原项目目录，分支 `codex/fork-shell-native`，没有保留 worktree 副本。

## 验收结论

实现与模拟器自动验收完成；游戏内验收待人工执行。不能将本记录视为游戏测试结果。

- 从 DiskA 的大端 MINIX 根文件系统加载独立的 init、sh 和命令程序。
- 真实 fork/exec/wait；固定窗口快照保持父子栈、内部指针和全局变量隔离；共享文件描述符偏移。
- 8 个用户槽耗尽与恢复、WNOHANG、64 次顺序创建/回收、孤儿收养及 PID 1 回收。
- 原生寄存器现场恢复探针输出 REG PASS；进程与执行文件探针输出 PROC PASS / EXEC PASS。
- ls、cat、echo、ps、sync；内建 cd/exit；shell 退出后重新启动；大小写、退格、提示符恢复。
- 连续 100 条外部命令全部产生预期输出，屏幕滚动后帧缓冲边界哨兵保持不变。
- 超过 TTY 队列容量的输入被有界处理，shell 丢弃超长行，下一条命令仍能运行。
- 磁盘文件写入、sync、保存镜像和重新启动后读取成功；超过单间接块容量的文件由客户机 cat 实际读完。
- 损坏可执行头、参数过大、指针越界、磁盘文件/目录元数据 zone、宿主帧缓冲地址回绕均有回归。

## 执行的验证

`mingw32-make test` 最终退出码为 0；该命令重建 M3 和交互系统镜像后执行全部测试。

'''+f'- C 编译器端到端：{count} 项通过。\n'+'''- 汇编器、模拟器、预处理器（30 项）、链接器（53 项）、M3 启动回归通过。
- platform、emu_io、user_link（跨 64KB 分支/函数指针）、tick 测试通过。
- shell_test 默认、--process、--stress、--tty 四种客户机执行模式通过。
- Python MINIX 镜像测试 1 项、CLI/磁盘/坏输入/持久化/双重间接块测试 7 项通过。
- 一次独立整体验证及后续问题复核完成；修正 yield 公平性、文件/目录 zone 校验与宿主帧缓冲边界。

源码与产物的完整测试输出留在本机 `.superpowers/full-test.log`，不作为项目源文件提交。

## 镜像摘要

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
'''+ '\n'.join(rows)+'''

## 游戏验收步骤

1. RAM 外部文件加载 build/kernel.bin；DiskA 外部文件加载 build/rootfs.bin。容量均为 8MB、Big endian。
2. 运行并确认屏幕出现 symplus$ 提示符；让键盘组件获得焦点。
3. 输入 ls /bin、cat /etc/motd、echo hello、forktest、proctest、execprobe、regtest。
4. 验证 Shift 大小写、退格、连续输出滚屏、cd /etc 后 cat motd，以及 exit 后 shell 重启。
5. 确认游戏实际键码映射、速度和持久 RAM 保存行为；若有差异，以实际游戏证据修正平台驱动。

所有内核镜像都使用二进制文件模式，不能向游戏粘贴大型 kernel.asm。

## 实现记录与计划调整

- 用户明确要求在原目录分支编辑，已迁回并删除临时 worktree；该约定记录在 AGENTS.md。
- 新链接逻辑放在 link_image.c，原 symld_link 接口保留；固定区与用户镜像用显式配置区分。
- 进程、待完成 syscall 与输入队列集中于 kernel/symplus/process.c；时钟、exec、磁盘驱动和磁盘编解码各自独立。
- 修正 C 自然布局以及显式磁盘编解码；未保留错误对齐的私有 MINIX 变体。
- 原 M3 独立输出到 build/m3，默认系统输出用于交互启动。构建脚本显式列举对象，避免残留对象混入。
- 以本记录列出的功能和验证作为原实施计划的最终执行结果；游戏人工验收仍未勾选。
'''
path=root/'docs/superpowers/2026-10-17-interactive-shell-verification.md';path.parent.mkdir(exist_ok=True);path.write_text(report,encoding='utf-8')
plan=root/'docs/superpowers/plans/2026-10-17-fork-disk-shell.md'
s=plan.read_text(encoding='utf-8');s+='\n\n## 最终执行状态（2026-10-17）\n\n功能实现与完整模拟器回归已完成；游戏人工验收待执行。原任务步骤为计划快照，文件合并/接口调整及逐项证据见 [最终验收记录](../2026-10-17-interactive-shell-verification.md)。\n'
plan.write_text(s,encoding='utf-8')
with zipfile.ZipFile(root/'build/symphonyplus-interactive.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in ['build/kernel.bin','build/rootfs.bin','README.md']:
        z.write(root/name,Path(name).name)
    z.write(path,'verification.md')
# Extract the actual successful framebuffer rows, retaining spaces in the recorded display.
part=log.split('\ntests/shell_test.exe\n',1)[1].split('\ntests/shell_test.exe --process',1)[0]
frame=[line for line in part.splitlines() if len(line)==96]
(root/'build/shell-screen.txt').write_text('\n'.join(frame[:40])+'\n',encoding='utf-8')
print(f'C tests: {count}; package bytes: {(root/"build/symphonyplus-interactive.zip").stat().st_size}')
print('\n'.join(rows))
