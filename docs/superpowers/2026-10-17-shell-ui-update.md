# Shell 交互与启动显示更新

本记录保留该次更新的验收与摘要。后续文件操作、heredoc 和 Shift 符号输入见
[文件操作更新记录](2026-10-17-file-shell-update.md)，当前镜像以该记录为准。

在原目录的 `codex/fork-shell-native` 分支实现，未创建 worktree。

## 行为

- `ls` 支持 `-a`、`-l`、组合参数、多个路径和 `--`。默认隐藏点文件；长格式显示类型、权限、链接数、UID、GID、字节数和名称。
- 提示符使用 `symplus:/当前目录$`，新增 `pwd`。从目录 inode 和父目录条目读取真实路径，启动嵌套 shell 时继承目录，失败的 `cd` 不改变目录。
- 内核逐阶段打印实际初始化状态；PID 1 打印 Welcome 后，通过客户机控制台的 ANSI 清屏及归位序列进入 shell。
- 原来将硬件键码 `0x0D` 转为 LF，导致游戏 Backspace 提交命令；现在 `0x0D` 删除字符，`0x0A` 提交，兼容退格 `0x08` / `0x7F`，忽略释放事件。

## 键码依据

用户明确反馈“按 Backspace 会提交命令并换行”。检查本机安装版本的游戏二进制，符号
`COM_KEYBOARD_KEYCODE_MAP__presenterZsetting95save95load_u1485` 的条目为
ImGui Backspace 523 → `0x0D`，Enter 525 → `0x0A`。只读取映射，没有修改游戏文件。
宿主终端 CR 仍由 `emu_main.c` 在输入边界转换为 LF，不与原始硬件事件混用。

## 验证范围

- `shell_test --ui` 执行真实用户程序，检查 `ls -la`、未知选项、目录提示符、嵌套 shell、`pwd` 和失败 `cd` 后的提示符；观察启动日志与 Welcome 曾显示，首次提示符前已清屏。
- `shell_test --keys` 注入游戏的 Backspace 按下/释放，验证 `echo keyx` 经删除和补写成为 `echo keyOK`，实际输出 `keyOK`，不提前执行 `keyx`。
- Python CLI 使用带隐藏文件的独立磁盘检查 `-la`、`-al`、`-a -l`、默认隐藏、多个文件操作数和 `--`。
- 保留全部 fork/exec、磁盘、长行、滚屏、旧 M3 和编译器回归。

用户截图已证实旧版系统能在游戏启动并输入命令；本次镜像更新仍须在游戏复验，模拟器结果不代替游戏实测。

## 完整回归结果与镜像

`mingw32-make test` 退出码 0，六种 shell 客户机执行模式、Python CLI 8 项、MINIX 镜像 1 项及其余完整回归通过。独立代码审阅通过。完整日志保存在本机 `.superpowers/shell-ui-full-test.log`。

| 镜像 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `build/kernel.bin` | 730708 | `f3c0efd8a13820ca9af75a76eeb3477eff32499c88501e391db1092628ebc8eb` |
| `build/rootfs.bin` | 8388608 | `d0b0b08ac5a6f569d5bf507cdba5add64652f649fee11c40ef53ca28fc84a80b` |
| `build/m3/kernel.bin` | 633280 | `68e5d6298d0294d81c98e484a720ad3a0eda87cc5376154b9c8ab3b7bec4ae3e` |

游戏复验：重新加载 RAM 与 DiskA 两个镜像，观察详细启动日志、Welcome 后清屏；输入 `ls -la /etc`、`cd /etc`、`pwd`、`sh`、`exit`，并在一条未提交的命令内按 Backspace，再按 Enter。
