# SymphonyPlus：在《Turing Complete》中运行 Linux 0.11 与 C 编译器

SymphonyPlus 是一套面向游戏《Turing Complete》中自建 32 位 CPU 的系统实验：从 C 编译器、链接器和指令模拟器，到 Linux 0.11 内核移植、磁盘文件系统和交互 shell。

当前系统可以在启动后输入命令、读写磁盘文件，并在客户机中运行原生 `symcc` 编译和执行 C 程序。原生编译实际经过客户机进程 `scpp → scc1 → sld`，使用真实的 `fork/exec/wait`。

**验证状态：** 模拟器已覆盖系统启动、进程、文件系统、shell 和原生编译流程。用户曾在游戏中确认 shell 启动、命令输入，并确认调整容量后启动乱码消失；最新性能优化与 Ctrl+C 组合键仍需实际游戏复验。

## 当前能力

| 部分 | 已实现 |
|---|---|
| CPU 与工具链 | 32 位大端 SymphonyPlus ISA；C 子集编译器、可重定位对象、链接器、汇编器和指令模拟器 |
| 内核与进程 | 基于 Linux 0.11 的移植；真实 fork、exec、wait；进程内存快照、退出状态和槽位回收 |
| 磁盘 | DiskA 块驱动、大端 MINIX v1 文件系统、目录与普通文件读写、同步与重启恢复 |
| 终端 | 96×40 字符屏幕；详细内核启动日志；Welcome 后清屏；带当前目录的提示符 |
| shell | ls、cat、mkdir、rm、重定向、heredoc、条件执行、reset 与 Ctrl+C |
| 原生 C 编译 | 单个源文件、SDK 头文件与运行库、编译阶段提示、成功后原子发布、失败或取消时保留旧输出 |

这是一个适配自定义 CPU 的实验系统，尚不提供完整 Linux 用户空间、POSIX 接口或 GCC 兼容环境。

## 快速开始

### 直接在游戏中加载

可以使用仓库内的 [32MiB 预编译系统包](build/SymphonyPlus-native-symcc-32MiB.zip)，或按下文从源码构建。将压缩包解压到固定目录，再设置外部文件路径。

| 游戏组件 | 外部文件 | 容量 | 端序 |
|---|---|---|---|
| 主 RAM | `kernel.bin` | **33554432 字节（32MiB）** | Big endian |
| DiskA | `rootfs.bin` | **33554432 字节（32MiB）** | Big endian |

屏幕连接主 RAM，键盘沿用 SymphonyPlus 的输入连接。更新系统时同时重新加载两个镜像并复位 CPU。

启动日志结束后，应出现：

```text
symplus:/$
```

先点击游戏键盘输入区域使其获得焦点，再输入命令。外部文件模式加载的是机器码 `.bin`，不能指向 `.asm` 文本，也不要将完整内核汇编粘贴到游戏汇编编辑器。

如果启动日志正常、进入 shell 前后出现乱码，先检查 RAM 和 DiskA 的**实际组件容量**。连接更大的外部文件不会自动扩大硬件容量；高地址回绕可能覆盖内核或屏幕数据。

### 从源码构建

当前构建流程使用 Windows、Git Bash、MSYS2 GCC 和 Python 3。确保 MSYS2 工具链排在 PATH 前部，避免混用 Git Bash 自带的其他 MinGW 环境。

PowerShell 示例（将路径换成实际安装位置）：

```powershell
$env:PATH = 'D:\Downloads\msys64\ucrt64\bin;' + $env:PATH
$env:PYTHONUTF8 = '1'

mingw32-make all
bash scripts/build_system.sh
```

也可以在 Git Bash 中运行：

```bash
export PATH="/d/Downloads/msys64/ucrt64/bin:$PATH"
export PYTHONUTF8=1
mingw32-make all
bash scripts/build_system.sh
```

构建成功后生成 `build/kernel.bin` 和 `build/rootfs.bin`，可直接按上表连接游戏组件。

**构建会重新生成根文件系统镜像。** 要保留游戏或模拟器中创建的文件，请先另存磁盘副本。

### 在宿主模拟器中运行

```powershell
.\emu\emu.exe build/kernel.bin --disk build/rootfs.bin --interactive
```

宿主交互界面按 Ctrl+D 退出。需要保存客户机对磁盘的修改时，明确指定另一个输出文件，并在客户机中执行 `sync`：

```powershell
.\emu\emu.exe build/kernel.bin --disk build/rootfs.bin --interactive --write-disk build/saved-rootfs.bin
```

默认不会覆盖输入磁盘。自动化运行还支持 `--input keys.txt`、`--until "屏幕文本"` 和 `--max-instructions N`。`--until` 也可能匹配命令回显，验收时应选择只在程序输出中出现的标记。

## shell 使用

提示符显示当前目录，例如 `cd /etc` 后为 `symplus:/etc$ `。

```sh
help
ls /bin
ls -la /etc
mkdir /tmp/demo && cd /tmp/demo
pwd
echo hello > note
echo world >> note
cat note
cat < note > copy
rm copy
cd /
rm -r /tmp/demo
sync
reset
```

- `cd`、`pwd`、`help`、`reset`、`exit [N]` 是内建命令；`exit` 后 init 会重新启动 shell。
- `ls` 支持 `-a`、`-l`、组合参数 `-la`、多个路径和 `--`；`mkdir` 支持多个目录，`rm -r` 递归删除目录。
- `&&` 在上一命令成功时执行，`||` 在失败时执行，`;` 顺序执行；列表从左到右求值。
- `>` 创建或覆盖文件，`>>` 追加，`<` 重定向输入。跳过的命令不会打开或截断重定向目标。
- 单引号、双引号可包住字面量；目前没有变量展开、命令替换、通配符展开、管道或后台作业。

### 用 cat 创建多行文件

```sh
cat > /tmp/note <<EOF
first line
second line
EOF
cat /tmp/note
```

单独一行 `EOF` 结束输入，不会写入文件。也兼容 `cat /tmp/note <<EOF` 的写文件简写。

heredoc 在打开输出文件前收集正文；超长行或暂存失败会报错并读到结束标记，避免将正文误当命令。正文单行最多 254 个字符加换行。

### 键盘与取消

- Enter 提交命令，Backspace 删除字符。
- 左右 Shift 独立处理，使用美式标点映射：`Shift+,` 输入 `<`，`Shift+.` 输入 `>`，也支持 `!@#$%^&*()_+{}|:"~?`。
- Ctrl+C 取消当前输入、heredoc 或前台命令，并返回 shell；被取消的命令状态为 130，同一命令列表的剩余部分不再执行。
- 原生编译取消时，symcc 清理临时文件并保留旧的可执行文件。

CPU 没有硬件中断。新编译的用户 C 程序在函数入口、循环和 goto 处包含软件检查点，每 4096 个检查点进入内核轮询，因此纯计算死循环也能响应取消。旧程序和没有检查点的手写汇编只能在系统调用处响应；更新镜像后应重新编译自己的程序。

SDK 用户 C 程序保留 `r12` 作为检查点计数器，手写汇编需遵守此约定。当前游戏键码适配为 Backspace=`0x0D`、Enter=`0x0A`，左右 Shift 为 16/20、左右 Ctrl 为 17/21；Ctrl 组合键仍需游戏复验。

## 在系统中编译 C

在游戏或模拟器的 shell 中输入：

```sh
cd /tmp
cat > main.c <<EOF
#include <stdio.h>
int main(void) {
    printf("Hello from native symcc!\n");
    return 0;
}
EOF
symcc main.c -o main && ./main
sync
```

标准头文件位于 `/include`，运行库对象位于 `/lib`。驱动会显示 `preprocessing`、`compiling` 和 `linking` 阶段。

客户机中的 `symcc` 一次接受一个源文件，支持 `-o output`（默认 `a.out`）以及 `-I dir` / `-Idir`。`-D/-E/-S/-c` 等选项在客户机驱动中尚未实现；宏可以写进源码或头文件。查看中间产物时可直接运行：

```sh
scpp main.c main.i -I /tmp
scc1 main.i main.sym
sld main.sym main.spx
```

日常执行程序请使用 `symcc`，由它处理执行权限和最终文件发布。语法错误、缺失头文件、链接失败、磁盘写满、内存耗尽或取消时，旧输出保持不变；成功后才原子替换输出。

宿主机上的 `symcc/symcc.exe` 是构建工具，支持多个源文件及 `-E/-S/-c/-I/-D/--d32` 等选项，其能力不能直接视作客户机驱动的命令行能力。

### C 功能范围

| 类别 | 当前实现 |
|---|---|
| 类型与数据 | 8 位 char、16 位 short、32 位 int/long/指针；有符号与无符号整数、枚举、固定大小数组、多维数组、结构体 |
| 表达式 | 算术、比较、逻辑、位运算、移位、复合赋值、递增递减、三目、逗号、转换、sizeof |
| 控制流与函数 | if/else、for、while、do/while、switch/case、break/continue、goto、递归、函数指针、当前 ABI 的可变参数、文件作用域 typedef |
| 预处理 | include、对象宏、函数宏、条件编译、defined、字符串化与标记拼接 |
| SDK | printf 等整数/字符串格式化、FILE 文件操作、常见字符串函数、malloc/calloc/realloc/free、整数转换、ctype |

当前是 C 子集，不承诺完整 C89/C99/C11 一致性：

- 没有浮点或真正的 64 位整数；`long long` 虽能解析，仍按 32 位处理。
- `union` 存在成员布局缺陷，应避免使用；不支持函数内部 typedef。
- const/volatile 等限定符的语义不完整，asm 文本不会生成实际内联汇编。
- SDK 不是完整 glibc；没有动态库、线程、网络或完整 POSIX API，printf 不支持浮点格式。

### 编译性能

同一个包含 `printf("test\n")` 的原生编译运行任务，历史模拟器测量由 **8,723,272,704** 降到 **3,978,405,888** 条目标指令，减少约 **54%**，约快 **2.19 倍**。最终结果已包含 Ctrl+C 软件检查点开销。

这些数字是目标指令计数，不是游戏中的秒表结果。按 19MHz、每指令一周期估算仍约需 3.5 分钟；实际还取决于 CPU 每条指令的周期数与游戏仿真速度。剩余主要成本在链接器的文本解析和符号查找，扩大总 RAM 不会直接消除这类计算。

## 系统边界

| 项目 | 当前配置 |
|---|---|
| RAM / DiskA | 各 32MiB，大端 |
| 用户进程槽 | 最多 8 个，包含 init、shell 和编译阶段进程 |
| 用户地址窗口 | 每进程固定 2MiB，包含代码、数据、堆和栈 |
| 栈保留 | 每进程 64KiB |
| 进程切换 | 系统调用边界上的合作式切换与窗口快照 |
| 内存保护 | 无硬件内存保护，不提供硬件抢占 |
| MINIX 文件名 | 每个路径分量最多 14 字节 |
| 编译阶段文本 | 读取上限 262144 字节，实际还受用户窗口约束 |

内存布局参数定义在 [runtime/platform.h](runtime/platform.h)。系统适配代码使用 `SYMPLUS-PORT` 标记。

## 验证与打包

```powershell
# 重建交互系统、历史 M3 镜像及容量探针，运行完整回归
mingw32-make test

# 完整测试后打包并逐字节核对归档内容
python scripts/package_system.py
```

测试包括工具链、内核启动、进程复用、终端滚屏、键盘、文件系统损坏与持久化、原生运行库、原生编译和多种 Ctrl+C 取消边界。宿主模拟器验收不替代实际游戏验收。

打包输出为 `build/SymphonyPlus-native-symcc-32MiB.zip`，包含系统镜像、独立容量探针、加载说明、内存与指令记录、SHA-256 清单。可选性能记录只在其镜像哈希匹配时加入。

单独检查游戏组件容量：

```powershell
bash scripts/build_capacity_probe.sh
```

临时将 RAM 指向 `build/capacity-probe.bin`、DiskA 指向 `build/capacity-disk.bin`，两者均设为 32MiB、大端。应显示 `RAM 32M PASS` 和 `DISK 32M PASS`；`ALIAS` 表示高地址回绕。探针使用独立测试盘并还原测试位置，验证后切回系统镜像。

## 项目结构

| 路径 | 内容 |
|---|---|
| [SymphonyPlus.isa](SymphonyPlus.isa) | CPU 指令集定义 |
| [symcc/](symcc/) / [symld/](symld/) | C 编译器和链接器 |
| [emu/](emu/) | 汇编器、CPU/磁盘/输入模拟器和宿主交互入口 |
| [Reference/Linux-0.11-main/](Reference/Linux-0.11-main/) | Linux 0.11 源码及 SymphonyPlus 适配 |
| [runtime/](runtime/) | 启动、ABI、平台布局、容量探针和运行时 |
| [user/](user/) | init、shell、用户命令、原生编译阶段与 SDK |
| [scripts/](scripts/) | 构建、根文件系统生成与打包 |
| [tests/](tests/) | 工具链、目标机器执行及系统回归 |
| `build/` | 生成的镜像、对象、日志和交付包；历史提交中也保留部分交付产物 |
| `docs/` | 本地设计与验收记录，已由 `.gitignore` 忽略，不随新提交跟踪 |

项目经历了 M0 环境验证、M1 编译器骨架、M2 内核编译链接、M3 启动后停机，现已推进到交互系统和原生编译。需要复现历史 M3 演示时，运行 `bash scripts/build_kernel.sh --m3`，镜像输出到 `build/m3/`；默认构建生成当前交互系统。
