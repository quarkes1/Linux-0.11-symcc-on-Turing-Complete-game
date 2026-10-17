# 当前交付：磁盘上的交互 shell

已实现真实 `fork/exec/wait`、合作式进程切换、DiskA 块驱动、大端 MINIX 文件系统及独立用户程序。
**模拟器端到端已验证；用户截图已确认旧版可在游戏启动并输入命令，本次更新仍需游戏复验。** 下文保留 M0–M3 的历史说明；当前默认构建以本节为准。

在项目原目录构建（MSYS2 GCC、Git Bash、Python 3）：

```powershell
$env:PATH = 'D:\Downloads\msys64\ucrt64\bin;' + $env:PATH
mingw32-make all
bash scripts/build_system.sh
mingw32-make test
```

模拟器交互运行，按 **Ctrl+D** 退出；客户机按键回显在帧缓冲中：

```powershell
.\emu\emu.exe build/kernel.bin --disk build/rootfs.bin --interactive
```

游戏加载：RAM 的外部文件设置为 `build/kernel.bin`，DiskA 的外部文件设置为 `build/rootfs.bin`；
两者均为 **8MB、Big endian**。屏幕需连接主 RAM，键盘连接现有 keyboard 输入。
启动时逐阶段打印 `[ OK ]` 内核日志，打印 Welcome 后清屏，出现 `symplus:/$ `。
点击游戏键盘输入区域使其获得焦点，再输入命令。
不要向游戏粘贴大型汇编源码。磁盘镜像会由构建脚本重新生成，保存游戏中修改的数据时另存副本。

```text
help
ls /bin
ls -la /etc
cat /etc/motd
echo hello
cd /etc
pwd
cat motd
ps
forktest
proctest
execprobe
sync
exit
```

`cd`、`pwd`、`help`、`exit` 是 shell 内建，其余列出的程序由磁盘加载；`exit` 后 PID 1 会重新启动 shell。
提示符显示当前绝对目录，例如 `cd /etc` 后显示 `symplus:/etc$ `。
`ls` 支持 `-a`（显示隐藏项）、`-l`（权限、链接数、UID、GID、字节数），可组合为 `-la` / `-al`，
可指定多个路径，并用 `--` 结束参数解析。默认隐藏以点开头的条目。
`forktest` 检查内存隔离、共享文件偏移及退出状态；`proctest` 检查槽耗尽、64 次回收和孤儿收养；
`execprobe` 检查损坏可执行文件、参数限制和指针边界。

模拟器默认不覆盖输入磁盘。需要保存时显式指定 `--write-disk saved-rootfs.bin`，并先在客户机执行 `sync`。
自动化可用 `--input keys.txt --until "屏幕文本"` 注入按键并在匹配后停止；`--until` 可能匹配回显文字，
断言命令执行结果应选输出专用标记。

当前限制：最多 8 个用户进程（含 init 和 shell）；每个进程有 512KB 用户窗口，工具链默认保留 64KB 栈空间。
采用调用边界的合作式切换，无系统调用的死循环不会被抢占。无硬件内存保护。
命令行支持空白分词、退格和大小写，不支持管道、重定向、引号和后台作业。
本机游戏键码为 Backspace=`0x0D`、Enter=`0x0A`；驱动将前者处理为删除，后者提交命令，
并兼容 ASCII 退格 `0x08` / `0x7F`。宿主模拟器在输入边界将终端 CR 转为 LF。
帧缓冲固定在 `0x2000`，链接器已明确避让；用户程序位于 `0x200000`，通过固定低地址网关完成长分支和系统调用。
游戏键码、真实运行速度和持久 RAM 保存行为应在游戏验收中确认。

原 M3 停机验收单独构建，不覆盖交互镜像：

```powershell
bash scripts/build_kernel.sh --m3
.\tests\kernel_boot_test.exe
```

新增实现设计与计划见 `docs/superpowers/specs/2026-10-17-fork-disk-shell-design.md` 和
`docs/superpowers/plans/2026-10-17-fork-disk-shell.md`。

---

<p align="center">
  <b>SymphonyPlus</b><br>
  在《Turing Complete》游戏搭建 32 位 CPU，<br>
  再用自研 C 工具链让 <b>Linux 0.11</b> 内核编译通过
</p>

<p align="center">
  <b>32 位大端 CPU</b> · <b>C89 子集编译器</b> · <b>链接器</b> · <b>指令模拟器</b><br>
  M0 环境验证 [完成] &nbsp;|&nbsp; M1 编译器骨架 [完成]&nbsp;|&nbsp; M2 内核编译演练 [完成]&nbsp;|&nbsp; M3 内核运行期 [完成]
</p>

---

## 这是什么

本项目是游戏《Turing Complete》（在游戏里用逻辑门搭建真实 CPU 的沙盒）的延伸工程：

1. **在游戏内设计并搭建了一颗 32 位 CPU —— SymphonyPlus**：16 个寄存器、定长指令、
   大端全链路、8 MB 主存 + 8 MB 磁盘、帧缓冲屏幕与键盘组件
2. **为它编写了一整套 C 交叉工具链**（全部 C 语言实现）：`symcc`（C89 子集编译器）+
   `symld`（链接器）+ 汇编器 + 指令模拟器
3. **把 Linux 0.11 内核源码移植编译**：50 个 `.c` 文件（含 x86 内联汇编的
   `SYMPLUS-PORT` 改写）全部编译、链接通过，输出 `KERNEL BUILD OK`

> M3 已完成：内核启动链在模拟器与游戏内跑通——11 行启动日志经 console 重定向
> 输出到游戏屏幕，fork 前停机。驱动/tick/调度归 M4/M5。

## 里程碑

| 里程碑 | 状态 | 内容 |
|---|---|---|
| **M0** 环境验证 | 完成（部分项待游戏内复核） | 加载基址、RAM/DiskA 容量（8MB）、帧缓冲协议、端序（大端） |
| **M1** 编译器骨架 | 完成（游戏内验收：屏幕显示 Hello） | symcc 单遍编译器、模拟器、屏幕运行时、有符号除/模 |
| **M2** 编译器特性 | 完成（编译+链接验收） | 结构体/数组/位域、预处理器、`.sym` 可重定位对象、symld 链接器、gcc 式驱动 CLI、**Linux 0.11 内核 50 个 `.c` 编译链接通过** |
| **M3** 内核运行期 | 完成（模拟器端到端 + 游戏内验收） | console 重定向到游戏屏（96×40 字节单元）、启动链逐阶段日志、fork 前停机、kernel.bin 外部文件模式加载 |

## 快速开始

依赖：Git Bash + MSYS2 MinGW（Windows）。

```bash
# Git Bash 下需 MSYS2 mingw 前缀（避免 DLL 冲突）
export PATH="/d/Downloads/msys64/mingw64/bin:$PATH"

mingw32-make              # 构建全部工具（编译器/链接器/汇编器/模拟器）
mingw32-make test         # 运行全部测试（六套：asm/emu/run/preproc/link/kernel_boot）
```

内核编译演练：

```bash
bash scripts/build_kernel.sh    # 50 个内核 .c → symcc → symld → kernel.asm → kernel.bin
                                # 期望输出：KERNEL BUILD OK
```

## 在游戏里运行你的 C 程序

1. 主机编译：`./symcc/symcc.exe main.c runtime/tty.c runtime/divsi3.c -o out.asm`（屏幕输出需链接 tty.c，见「使用教程」）
2. 游戏内加载，两种方式任选：
   - 把 `out.asm` 全文粘贴进 RAM 的 **"Edit assembly"**（游戏汇编器现场汇编）
   - 或先 `./emu/asm.exe out.asm out.bin`，把 RAM 的"外部文件"指向 `out.bin`
     （文件模式**原样装载字节、不做汇编**，必须喂 .bin 机器码而不是 .asm 源码）
3. 运行（屏幕组件需链接到主内存；RAM 与 DiskA 均为 **Big endian**、8 MB）
4. 屏幕输出即为程序结果（M1 验收：左上角显示 Hello）

## 运行 Linux 0.11 内核（M3）

构建：

```bash
bash scripts/build_kernel.sh    # 50 个内核 .c → symld → kernel.asm → kernel.bin（约 570KB）
```

游戏内验证：

1. RAM 的"外部文件"指向 `build/kernel.bin`（原样装载字节，不做汇编）
2. 运行：屏幕从左上角逐行显示 11 行启动日志（mem_init OK ... Boot complete. Halting.）
3. 程序停在 halt 自循环（空转），游戏 UI 不冻结

> 红线：不要向游戏粘贴 kernel.asm 全文——3.4MB 文本会让游戏每次进沙盒
> 重新汇编而卡死（只能改存档恢复）。内核验证一律走 kernel.bin 文件模式。

## 使用教程

### 最小可运行程序

`hello.c`：

```c
int main() {
    putstr("Hello, SymphonyPlus!");
    putchar(10);            /* 换行 */
    return 42;              /* main 的返回值 = 模拟器 exit_code */
}
```

编译（全链路：预处理 → 编译 → 统一链接 → 绝对地址 asm）：

```bash
./symcc/symcc.exe hello.c runtime/tty.c runtime/divsi3.c -o hello.asm
```

本机验证（无需进游戏）：

```bash
./emu/asm.exe hello.asm hello.bin    # 汇编 → bin
./emu/emu.exe hello.bin              # 模拟执行，打印 exit_code = 42
```

游戏内加载两种方式（见上一节）：
- 粘贴 `hello.asm` 全文 → 游戏汇编器现场汇编
- RAM "外部文件"指向 `hello.bin` → 原样装载字节。文件模式不做汇编，喂 .asm 源码
  会把文本字节直接装进内存（与汇编结果完全不同）；务必用 `emu/asm.exe` 生成的
  .bin（大端 32 位指令字，与游戏 RAM 格式一致）

要点：
- **屏幕输出要链接 `runtime/tty.c`**，并同时带上 **`runtime/divsi3.c`** —— `putchar` 内部用 `%` 换行，缺 `divsi3.c` 时链接报 `undefined jump target: __modsi3`
- **有符号 `/` 和 `%` 要链接 `runtime/divsi3.c`** —— 硬件只有无符号除法，编译器把 `a / b` 自动编译成 `call __divsi3`
- 多文件 = 命令行写多个 `.c`，一起编译链接；无选项时输出绝对地址 asm

### 常用 API

屏幕输出（`runtime/tty.c`）：

| 名称 | 签名 | 说明 |
|---|---|---|
| `putchar` | `int putchar(int c)` | 输出一个字符；识别 LF(10) 换行、CR(13) 回行首、退格(8) |
| `putstr` | `int putstr(char *s)` | 输出字符串 |
| `cursor` | `int cursor`（全局变量） | 光标位置 0..96×40，可读写 |
| `fb` | `char *fb`（全局变量，初值 0x2000） | 帧缓冲指针，`*(fb + i)` 直接读写屏幕字符 |

有符号除法（`runtime/divsi3.c`，编译器对 `/` `%` 自动调用）：

| 名称 | 签名 |
|---|---|
| `__divsi3` | `int __divsi3(int a, int b)` |
| `__modsi3` | `int __modsi3(int a, int b)` |

注意：
- 以上函数**没有头文件**，直接调用即可（编译器预扫描会把未声明函数全局注册）
- 没有 printf / malloc / 任何 libc —— 需要什么函数就自己写（`runtime/tty.c` 是无依赖纯 C 的最佳样板）

### #include 与头文件

内置头文件目前只有一个：`symcc/include/config.h`（目标机配置常量）：

```c
#define FRAMEBUF_BASE 0x2000   /* 屏幕帧缓冲基址（ASCII 8 模式） */
#define COLS 96                /* 每行字符数 */
#define ROWS 40                /* 总行数 */
```

```c
#include "config.h"
int main() {
    putchar('0' + COLS / 10);   /* 用宏写 96×40 屏幕逻辑 */
    putchar('0' + COLS % 10);
    putchar(10);
    return ROWS;
}
```

```bash
./symcc/symcc.exe -I symcc/include prog.c runtime/tty.c runtime/divsi3.c -o prog.asm
```

搜索规则（与 gcc 一致）：
- `#include "file.h"` → 当前文件所在目录 → `-I` 目录 → 当前工作目录
- `#include <file.h>` → 仅 `-I` 目录

### 分步编译与 CLI 参考

| 命令 | 产物 |
|---|---|
| `symcc -E foo.c -o foo.i` | 只预处理（查看宏/头文件展开结果） |
| `symcc -S foo.c -o foo.s` | 可重定位 asm（.sym 文本对象） |
| `symcc -c foo.c -o foo.sym` | .sym 对象（多文件分步编译） |
| `symld a.sym b.sym -o out.asm [--bin out.bin] [--crt0 path]` | 独立链接器 |
| `symcc a.c b.c -o out.asm` | 全链路：预处理 → 编译 → 统一链接 → 绝对 asm |

其余选项：`-I <dir>`（可多个）、`-D NAME[=VAL]`（预定义宏，`-DNAME` 连写亦可）、`-save-temps`（保留 .i/.s/.sym 中间文件）、`--d32`（强制数据引用走 32 位装载）、`-v`（打印各阶段文件名）。

说明：symcc 全链路自动带 `runtime/crt0.asm` 启动模板（屏幕 ASCII 模式与帧缓冲基址、栈顶 1MB（0x100000，避开文字池）、清屏与 bss 清零、`call main`）；`symld` 单独链接时 `--crt0` 默认也是它，可省略。

## 技术概览

### 工具链

| 工具 | 作用 |
|---|---|
| `symcc` | C89 子集编译器。gcc 式驱动：`-E`（预处理）/ `-S`（可重定位 asm）/ `-c`（.sym 对象）/ 无选项 = 全链路出绝对 asm；`-I` `-D` `-save-temps` `--d32` 等 |
| `symld` | 链接器：`.sym` 对象 → 统一布局 → 绝对地址 asm（支持多文件、重定位、跳转桩化） |
| `emu/asm.exe` | 两遍汇编器（asm → bin），指令表与 `SymphonyPlus.isa` 逐字一致 |
| `emu/emu.exe` | 指令模拟器：本机调试，无需进游戏（执行 + 退出码 + 内存断言） |

### 编译语言能力（M2）

- **类型**：struct/union/enum、数组、位域、typedef、函数指针、类型转换、
  存储类（static/extern/const…）、`long`/`long long`（32 位同宽）
- **运算符**：位运算全套、复合赋值、`++`/`--`、`?:`、`sizeof`、逗号
- **语句**：`switch`/`goto`/`break`/`continue`/`do-while`
- **预处理**：宏、条件编译、`#include`
- **ABI**：全栈传参、struct 按值传参/返回、`va_start` 可变参数

### 寻址约束（游戏 ISA 带来的挑战）

SymphonyPlus 的立即数跳转只有 16 位绝对地址（≤64 KB），而内核代码远超 64 KB。
解决方案（**不改动 ISA**）：

```
低区 <64KB:  crt0 → jmp halt → 文字池（跳转目标表）→ 跳板 → bss
高区 >64KB:  所有对象代码 + data
```

- `call sym` → 24 字节桩（存返回地址 → 池槽全 32 位装载 → jmp）
- `jmp sym` → 8 字节桩；条件跳转 → 跳板（池槽去重，同目标共享）
- 数据引用：低区 bss 直接 16 位；data/函数 → 编译期 32 位拆装（`@hi:/@lo:`）

### 内核移植（SYMPLUS-PORT）

Linux 0.11 大量使用 x86 内联汇编（`__asm__`、寄存器绑定、语句表达式宏）。
每个改写点以 `/* SYMPLUS-PORT: 原因 */` 注释标记，改为纯 C 或 stub ——
git 可追溯、差异可见。运行期语义验证归 M3。

## 项目结构

```
TuringComplete/
├── symcc/                    # C 交叉编译器（tokenize→preprocess→parse→codegen）
├── symld/                    # 链接器（.sym 对象 → 绝对 asm）
├── runtime/                  # 运行时（crt0.asm 启动模板 + tty.c + divsi3.c）
├── emu/                      # 本地汇编器 + 模拟器（调试用，无需进游戏）
├── tests/                    # 端到端测试（make test，六套）
├── scripts/                  # build_kernel.sh：内核编译演练验收（产出 kernel.bin）
├── Reference/Linux-0.11-main/# 内核源码（SYMPLUS-PORT 适配）
└── SymphonyPlus.isa          # 游戏指令集定义（与 emu/isa.c 同步）
```

## 文档

- [M2 编译器设计文档](docs/superpowers/specs/2026-08-20-m2-compiler-design.md) —
  语言能力边界、ABI、.sym 对象格式、链接器行为
- [Linux 0.11 移植设计](docs/superpowers/specs/2026-08-20-symcc-linux011-port-design.md) —
  SYMPLUS-PORT 策略与内核改写点
- [M3 内核运行期验证设计](docs/superpowers/specs/2026-08-21-m3-kernel-runtime-design.md) —
  console 重定向、启动链日志、停机点与验收标准

## 致谢

- **Linux 0.11** —— Linus Torvalds 的经典内核源码（本仓库 `Reference/Linux-0.11-main/`，MIT 协议）
- **《Turing Complete》游戏** —— 在游戏里实现真实 CPU 的奇妙沙盒
