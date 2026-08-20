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

1. 主机编译：`symcc main.c -o out.asm`
2. 复制 `out.asm` 全文
3. 游戏沙盒：主内存 RAM 右键 **"Edit assembly"** → 粘贴 → 运行
   （屏幕组件需链接到主内存；RAM 与 DiskA 均为 **Big endian**、8 MB）
4. 屏幕输出即为程序结果（M1 验收：左上角显示 Hello）

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
