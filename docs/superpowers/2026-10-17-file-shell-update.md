# 文件操作、heredoc 与 Shift 输入

在原项目 `codex/fork-shell-native` 分支实现，未创建 worktree。

## 可用命令

```sh
mkdir /tmp/demo
cat > /tmp/demo/main.c <<EOF
int main(void) { return 2 < 3; }
EOF
cat /tmp/demo/main.c
echo another line >> /tmp/demo/main.c
cat < /tmp/demo/main.c > /tmp/copy
rm /tmp/copy
rm -r /tmp/demo
sync
```

也兼容用户要求的 `cat file <<EOF` 简写；该形式将正文写入 file，覆盖原内容。
heredoc 显示 `> ` 续行提示符，正文按原样保存，结束标记独占一行且不进入文件。
先将正文流式写入已 unlink 的临时文件，收集成功后再打开目标，避免失败时截断目标。
临时文件使用 O_EXCL，退出后关闭并回收，不保留临时名字；临时文件创建失败或正文行过长时也消费到结束标记。

文件系统调用使用 Linux 原有实现：unlink=10、mkdir=39、rmdir=40，入口检查客户机字符串地址。
mkdir/rm 为独立磁盘程序，经真实 fork/exec/wait 运行；cat 支持标准输入和 `-`。
rm -r 遍历 MINIX 目录，包含隐藏项，拒绝根目录及以 `.`、`..` 结尾的操作数。
shell 为内建与外部命令保存、重定向及恢复标准输入/输出，错误输出保持终端。

## 输入与边界

左右 Shift 使用独立状态，松开其中一个不清除另一个。美式符号键支持 `< > ! @ # $ % ^ & * ( ) _ + { } | : " ~ ?`。
保留游戏 Backspace=0x0D、Enter=0x0A 的区分。宿主输入文件同时接受 LF、CRLF 和 CR，CRLF 不增加空行。
命令/正文每行最多 254 个字符加换行；最多 32 个参数，每条命令最多一个输入和一个输出重定向。
支持字面量单/双引号和引号外的反斜杠转义，不展开变量、通配符或命令，不支持管道和后台任务。

## 关联修复

- 平面镜像链接器先跳过缩进再识别字符串数据，防止字符串内的 `;` 和 `@` 被当作注释或重定位。
- 文件截断和删除的间接块读取必须位于合法数据 zone 范围，不能把元数据块当作间接表解释。

## 游戏内编译的可行性

用户已明确接受以 symcc 替代 GCC。后续目标为原生客户机工作流：在游戏中编写 main.c，执行 `symcc main.c -o main`，再执行 `./main`。该命令形式是目标接口，当前镜像尚未提供客户机 symcc。

可以将“编辑 C → 编译 → 执行”作为后续目标；本次没有安装客户机编译器。
完整 GCC 需要 SymphonyPlus 的机器描述、目标后端和相关运行环境，官方移植接口见
[GCC Back End](https://gcc.gnu.org/onlinedocs/gccint/Back-End.html)。现有 Windows/x86 编译器二进制不能直接由当前 CPU 执行。

更贴近项目的路线是移植现有 symcc，但尚未验证自举可行性。代码检查已确认：

- `symcc/src/main.c` 依赖 FILE、fopen/fread/fseek/ftell、malloc 及格式化 I/O；客户机 libc 目前只有基础 syscall 和字符串函数。
- `symcc/src/symcc.h`、codegen 等使用 int64_t；当前目标解析器将 long long 作为 32 位处理，不能假定直接编译自身源码就语义正确。
- 当前每进程 512KB 窗口，需要先评估编译器代码、数据、堆和栈占用，并为生成的 SPX 程序补齐运行库/链接及执行权限流程。

建议先验证小型原生编译器能编译简单程序，再扩展 C 子集、自举及工具链；不将“编译 symcc 的源码成功”当作客户机编译器验收。

## 完整验收

`mingw32-make test` 退出码 0，包含八种 shell 客户机模式、Python CLI 15 项（损坏间接块测试含 6 个子情形）、MINIX 镜像测试及其余编译器/链接器/模拟器/M3 回归。独立审阅发现的间接块问题已复现、修复并复核通过。

- 真实 Shift+标点键事件生成重定向、heredoc 与 C 代码，客户机实际写入、读取并删除文件。
- 保存并重新启动读取新文件；追加、输入文件重定向、空 heredoc、兼容简写、隐藏文件递归删除通过。
- 错误语法不截断文件，失败 heredoc 不执行正文，过长行读到结束标记，重复 heredoc 不泄漏描述符或临时文件名。
- 完整日志保存在本机 `.superpowers/file-shell-full-test.log`。游戏内新版组合键与交互仍待用户复验，模拟器结果不作为游戏实测。

| 镜像 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `build/kernel.bin` | 733552 | `127b8fede0010cca3ff5352932863a0b3289111702218901036e98f587d6f68a` |
| `build/rootfs.bin` | 8388608 | `0fe076b949cd0fb371e65a60727fa9c27b20d61114c48389977c9b24fe94b9b1` |
| `build/m3/kernel.bin` | 633908 | `364a9f73ecf9f8b795b0a6a3b91acb0d5b35dec4d4b13775b6053efa16b6936f` |
