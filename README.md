<p align="center">
  <b>SymphonyPlus</b><br>
  在《Turing Complete》游戏搭建 32 位 CPU，<br>
  再用自研 C 工具链让 <b>Linux 0.11</b> 内核编译通过
</p>

<p align="center">
  <b>32 位大端 CPU</b> · <b>C89 子集编译器</b> · <b>链接器</b> · <b>指令模拟器</b><br>
  M0 环境验证 [完成] &nbsp;|&nbsp; M1 编译器骨架 [完成]&nbsp;|&nbsp; M2 内核编译演练 [完成]
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

> 内核运行期验证（启动链、驱动）是下一个里程碑 M3。

## 里程碑

| 里程碑 | 状态 | 内容 |
|---|---|---|
| **M0** 环境验证 | 完成（部分项待游戏内复核） | 加载基址、RAM/DiskA 容量（8MB）、帧缓冲协议、端序（大端） |
| **M1** 编译器骨架 | 完成（游戏内验收：屏幕显示 Hello） | symcc 单遍编译器、模拟器、屏幕运行时、有符号除/模 |
| **M2** 编译器特性 | 完成（编译+链接验收） | 结构体/数组/位域、预处理器、`.sym` 可重定位对象、symld 链接器、gcc 式驱动 CLI、**Linux 0.11 内核 50 个 `.c` 编译链接通过** |

## 快速开始

依赖：Git Bash + MSYS2 MinGW（Windows）。

```bash
# Git Bash 下需 MSYS2 mingw 前缀（避免 DLL 冲突）
export PATH="/d/Downloads/msys64/mingw64/bin:$PATH"

mingw32-make              # 构建全部工具（编译器/链接器/汇编器/模拟器）
mingw32-make test         # 运行全部测试（五套：asm/emu/run/preproc/link）
```

内核编译演练：

```bash
bash scripts/build_kernel.sh    # 50 个内核 .c → symcc → symld → kernel.asm
                                # 期望输出：KERNEL BUILD OK
```

## 在游戏里运行你的 C 程序

1. 主机编译：`./symcc/symcc.exe main.c runtime/tty.c runtime/divsi3.c -o out.asm`（屏幕输出需链接 tty.c，见「使用教程」）
2. 复制 `out.asm` 全文
3. 游戏沙盒：主内存 RAM 右键 **"Edit assembly"** → 粘贴 → 运行
   （屏幕组件需链接到主内存；RAM 与 DiskA 均为 **Big endian**、8 MB）
4. 屏幕输出即为程序结果（M1 验收：左上角显示 Hello）

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

然后把 `hello.asm` 全文粘贴进游戏 RAM（见上一节）。

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

说明：symcc 全链路自动带 `runtime/crt0.asm` 启动模板（屏幕 ASCII 模式与帧缓冲基址、栈顶 0x4000、bss 清零、`call main`）；`symld` 单独链接时 `--crt0` 默认也是它，可省略。

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
├── tests/                    # 端到端测试（make test，五套）
├── scripts/                  # build_kernel.sh：内核编译演练验收
├── Reference/Linux-0.11-main/# 内核源码（SYMPLUS-PORT 适配）
└── SymphonyPlus.isa          # 游戏指令集定义（与 emu/isa.c 同步）
```

## 文档

- [M2 编译器设计文档](docs/superpowers/specs/2026-08-20-m2-compiler-design.md) —
  语言能力边界、ABI、.sym 对象格式、链接器行为
- [Linux 0.11 移植设计](docs/superpowers/specs/2026-08-20-symcc-linux011-port-design.md) —
  SYMPLUS-PORT 策略与内核改写点

## 致谢

- **Linux 0.11** —— Linus Torvalds 的经典内核源码（本仓库 `Reference/Linux-0.11-main/`，MIT 协议）
- **《Turing Complete》游戏** —— 在游戏里实现真实 CPU 的奇妙沙盒
