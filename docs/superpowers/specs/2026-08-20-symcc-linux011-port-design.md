# SymphonyPlus Linux 0.11 移植项目 — 设计文档

日期：2026-08-20
状态：已获用户分节确认（第 1–4 节）

## 1. 背景与目标

用户在图灵完备（Turing Complete）游戏中用逻辑门搭建了一台 32 位计算机 "SymphonyPlus"
（自定义指令集，见 `SymphonyPlus.isa`），最终目标是在其上**移植运行 Linux 0.11**。

项目定位（用户确认）：
- **允许移植/修改**内核源码，非原版直跑
- **教学目的优先**，性能与启动时间不敏感
- 指令集、硬件**不再改动**（现有 CPU 成品不改；M7 为可选升级）
- I/O 走游戏内**屏幕 + 键盘**组件（键盘监听真实键盘），达成虚拟机终端效果
- 终端通道约定：`out`/`in` 仅传输数字量，**不作为字符串终端**；所有文本输出走屏幕
  帧缓冲，输入走键盘组件（见 §2.3、§5.1）
- 工作目录：`C:\Users\19444\Desktop\TuringComplete`；内核源码已置于 `Reference/Linux-0.11-main/`

路线决策（用户已确认）：
1. **C 交叉编译器自研**（chibicc 风格单遍式，C 语言实现，PC 端运行，输出 .asm）
2. **去 MMU 化移植**：无分页、无特权级、fork 整段复制（uClinux 风格）
3. **轮询式调度**：无硬件中断，空闲循环读 time_0 推进 jiffies；tick 抽象为薄层，
   M7 升级为硬件中断时接口不变
4. **内存/硬盘配置小端序**（用户已在游戏内完成）

## 2. 探索发现（事实依据）

来源：游戏安装目录逆向（exe 字符串、campaign 关卡测试、asset/manual 手册页）+ 社区调研。

### 2.1 指令集（SymphonyPlus.isa）
- 16 寄存器：zr, r1–r13, sp, flags；固定 32 位指令（部分伪指令 8/12/16 字节展开）
- 三操作数 ALU；带符号/无符号条件跳转齐全；load/store 8/16/32（[reg] 或 [U16 imm]）
- 外存 pload/pstore 仅 32 位字访问
- **无 base+offset 寻址、立即数仅 16 位、跳转目标仅 16 位绝对地址（0–65535）**
- 仅无符号 mul/div/mod → 有符号运算需运行时库
- flags 被一切 ALU 与 call/ret 覆盖；call/ret 以 flags 存返回地址
- **通用指令格式**（用户确认，2026-08-20）：32 位按 8 个 4 位组书写
  （组 1 = bits 31-28 … 组 8 = bits 3-0）。三操作数指令为
  `0MM0 oooo dddd aaaa 0000 bbbb 0000 0000`（寄存器形式，bit4=0）/
  `0MM1 oooo dddd aaaa bbbb bbbb bbbb bbbb`（立即数形式，bit4=1，imm@0-15）。
  操作数位置：第一操作数 d@23-20、第二 a@19-16、第三 b@11-8。
  **特例**（两操作数/寻址类指令不套用 d 位置）：
  cmp = a@19-16、b@11-8（d 固定 1111）；jmp reg = reg@11-8；
  load_* = dest@23-20、adr@11-8（a 位置固定 0000）；
  store_* = value@19-16、adr@11-8；out reg / screen = a@19-16、b@11-8。
  模拟器（emu/emu.c）已按此实现并经 6 项语义测试验证。

### 2.2 游戏汇编器（关键行为）
- 双遍扫描；label 值 = 指令字节地址（**绝对地址**）
- 立即数/label 溢出是硬错误（"Immediate too large"），**不截断** → 编译器必须自行保证
  所有 label 引用落在 16 位内（64KB 分段模型之依据）
- 支持：`;`/`//` 注释、`name:` label、`U8/U16/U32/U64` 裸数据、字符串字面量、
  `@addr` 对齐、`const`、`include`/`pub`、`[settings] endianness`
- ISA 指令编码默认大端；内存组件端序可配置（"Little endian" 复选框），但**端序设置同时影响指令读取**——2026-08-20 用户实测：改为小端后指令无法执行（游戏汇编器输出固定大端字节流，小端模式下被反读）→ 全链路统一大端（见 §3.1）

### 2.3 硬件组件协议
- **RAM**：字节寻址；容量 = 2^ADDR_WIDTH 字节（2 的幂）；Init data 可选
  Zeroes/Assembler/Punch Card/File/Hex Editor/Persistent；右键 Edit assembly
- **硬盘 = 持久 RAM**（用户实例名 "DiskA"）：Persistent 标志，重启不清空；
  配 Init data = Assembler + .asm 文件 → 自动汇编为二进制存入该 RAM
- **屏幕**：8 个设置寄存器（setting[0]=模式, [1]=数据偏移, [2]=前景色, [3]=背景色,
  [4]=字体）；模式 0 = ASCII 8（96×40 字符，1 字节/字符）；必须链接一块 RAM 作显存，
  读数据偏移 = setting[1]；`screen` 指令写 setting[A]=B
- **字符显示机制**：模式 0 下屏幕持续读取链接 RAM（数据偏移起）的字节作为字符 →
  显示文本 = 程序用 `store_8` 向帧缓冲区写字节（96×40 网格，行步长 96）
- **键盘**：FIFO，每周期至多 1 事件；值 = `(按下<<8)|键码`；空队列返回 0
- **时钟**：纳秒级 64 位；`time_0` 读低 32 位并锁存高 32 位给 `time_1`
  （time_1 在 time_0 未执行时为 0）
- **counter**：每周期 +1，指令为 4 字节定长时即每指令 +4

### 2.4 社区先例
- TCMIPS（zhangjiantao/tcmips）：LLVM 工具链 + 游戏内 MIPS32 CPU，跑起 Windows 95
  （启动 2h19m / 1030 亿 tick）与 Mac OS；**尚无人在 TC 上公开启动 Linux** → 本项目为首次
- 官方 isa_spec 仓库（Nim）与 wiki 为 .isa 格式权威文档

## 3. ABI 与内存模型

### 3.1 端序：大端（2026-08-20 实测修正）
**实测**：RAM 组件的端序设置同时影响指令读取——用户将内存改为 Little endian 后
指令无法执行（游戏汇编器输出按 .isa 位模式字面顺序 = 大端字节流，小端模式下
被反读，首字节变成 opcode）。游戏汇编器输出不随端序设置变化。
**结论**：内存与 DiskA 统一配置 **Big endian**；指令与数据全链路大端；
ABI 由小端改为大端。
对 Linux 0.11 移植的影响：0.11 无网络协议栈（无字节序敏感的协议头处理）；
MINIX 磁盘镜像由我们全权构建（PC 端工具按大端写盘，内核 fs 按 CPU 端序读，
天然自洽）；与外部小端数据无交互（键盘仅 ASCII 码）。几乎无影响。

### 3.2 内存布局（固定基址 0，编译期已知）
```
0x000000 ┌──────────────┐
         │ Bank0 代码   │ ← 引导程序从 DiskA 逐 bank 拷入
         ├──────────────┤
         │ Bank1 代码…  │   （每 bank ≤ 64KB）
         ├──────────────┤
         │ 数据区        │ ← r13 (gp) 基址
         ├──────────────┤
         │ 堆（向上）    │
         ├──────────────┤
         │ 栈（向下）    │ ← sp 初始 = RAM 顶
RAM 顶   └──────────────┘
```
- RAM 容量取 2 的幂（如 4MB）；DiskA 容量容纳全部 bank 镜像 + MINIX 盘数据
- 引导程序（手写 .asm，放独立 RAM 或主内存顶部）：初始化 sp/gp、清 bss、
  逐 bank pload → 跳内核入口

### 3.3 寄存器约定
| 寄存器 | 用途 |
|---|---|
| r1–r9 | 临时/表达式（caller-saved） |
| r10–r12 | callee-saved |
| r13 (gp) | 全局基址（保留） |
| sp / zr / flags | 栈 / 零 / 恒脏 scratch（ALU 与 call/ret 均覆盖） |

### 3.4 调用约定
- **全部参数走栈**（从右往左 push），返回值 r1 —— 天然支持 variadic（printf 需要）
- 无帧指针（ISA 无 offset 寻址，帧指针无价值）；局部变量固定 sp-relative，
  访问时 add 合成地址
- 对齐 4 字节

### 3.5 大内存模型（64KB 分段）
- 编译器以基址 0 计算所有 label 绝对地址
- bank 内：直接 call / 条件跳转；**条件跳转仅 imm16 → 目标必须同 bank**（编译器保证）
- 跨 bank 调用：构造返回地址常量 → push → 构造目标地址 → `jmp reg`
- 跨 bank 条件跳转：条件取反 + 跳过式长跳转
- 32 位常量：同 bank 文字池，`load_32 [imm16]` 取
- 全局数据访问：`add` gp+delta 后寄存器寻址 load/store（delta ≤ 65535 每 bank）

## 4. 编译器设计（symcc）

### 4.1 C 语言子集
支持：int/char/short/long(32)、指针、数组、struct/union/enum、函数指针、switch、goto、
static、unsigned、位运算、字符串字面量、完整预处理器（#include/#define/#if…）、variadic。
不支持（刻意）：long long（内核几乎不用，遇到再补）、float/double（内核无浮点）、
GNU 扩展（内联汇编等 → 内核去 asm 化路线）。

### 4.2 架构
- tokenize → 递归下降解析 → 代码生成：单遍（chibicc 风格）
- **+ 布局 pass（本 ISA 特有）**：记录每条指令字节偏移（指令长度 4/8/12/16 不等）→
  bank 切分 → 远跳转重写 → 跨 bank 调用展开 → 文字池布局
- 输出保证所有 label 引用 ≤ 65535

### 4.3 寄存器分配与代码生成
- 表达式滚动分配 r1–r9，溢出到栈；局部变量全在栈上
- 轻量 peephole：load+op+store 折叠、mov 消除、常量合并
- 不做图染色（教学优先，后期可优化）

### 4.4 输出契约
汇编器原生特性：`;` 注释、`name:`、`U32 0x…`、`"字符串"`、`@0x…` 对齐。

### 4.5 运行时库（runtime/）
- `__mulsi3`/`__divsi3`/`__modsi3`（有符号，ISA 仅无符号）
- memcpy/memset/memcmp/strlen 等（手写 .asm 或 C 编译）

## 5. 内核移植策略（M5）

### 5.1 改造地图
| 模块 | 处理 |
|---|---|
| boot/ (bootsect.s, setup.s, head.s) | 🆕 重写为 SymphonyPlus 引导 |
| kernel/system_call.s | 🆕 重写：直接调用式系统调用入口（无 trap） |
| kernel/sched.c | ✏️ switch_to 内联 asm 外置；do_timer 轮询化（tick 薄层） |
| mm/memory.c, mm/page.s | ✏️ 删分页 → 位图分配器；fork 整段复制 |
| kernel/fork.c, exec.c | ✏️ copy_mem 重写；a.out 加载改平坦（或自定义格式，M5 定） |
| kernel/blk_drv/hd.c | ✏️ 重写为 pload/pstore 驱动（DiskA） |
| kernel/chr_drv/ | ✏️ console.c + keyboard.S → screen/keyboard 驱动（文本写帧缓冲 RAM，ASCII 8 模式）；serial 删 |
| kernel/chr_drv/tty.c | ✏️ 保留 tty 概念，输出走屏幕帧缓冲 |
| kernel/time.c | ✏️ 轮询 jiffies（time_0 锁存语义已确认） |
| fs/ | ✅ 几乎全保留（MINIX v1，纯 C） |
| kernel/sys.c, signal.c, exit.c | ✅ 大部分保留 |
| include/ | ✏️ unistd.h 等系统调用宏去 asm 化 |
| lib/ | ✅ 保留 |

### 5.2 三条主线
1. **去 asm 化**：内联汇编 → `kernel/arch/` 手写汇编 + 纯 C 替代 → 编译器只需标准 C
2. **去 MMU 化**：fork = 整段复制；exec = 平坦加载
3. **轮询化**：tick 薄层——空闲循环读 time_0 推进 jiffies；M7 换硬件中断接口不变

### 5.3 系统调用与用户态
- 用户程序 libc 包装函数直接 call 内核入口（无特权级下的"系统调用"）
- 进程保持语义：独立地址空间分配（整段复制 fork）
- 用户程序 a.out（或自定义格式）；MINIX 镜像 PC 端构建 → 写入 DiskA

## 6. 里程碑路线图

| # | 里程碑 | 游戏内验收 |
|---|---|---|
| M0 | 环境验证 | 端序已配置 ✅；RAM/DiskA 容量确认；asm 加载链路实测 |
| M1 | 编译器骨架 | int/指针/算术/if/while/函数调用 → 屏幕帧缓冲显示 "Hello" |
| M2 | 类型系统+运行库 | struct/数组/switch/全局变量；signed div/mul/mod；printf 子集；**内核源码 C 特性审计** |
| M3 | 大内存模型 | 程序超 64KB 正确运行（bank 切分/远跳转/文字池） |
| M4 | 启动+最小内核 | boot、printk→屏幕、轮询 tick、协作式调度、直接调用式 syscall |
| M5 | 内核移植 | 位图分配器、memcpy fork、内联汇编外置、MINIX 文件系统、设备驱动 |
| M6 | 用户态 | shell + 基础命令；多进程轮转可见 |
| M7 | （可选）抢占升级 | 游戏加中断控制器 → 硬件 tick → 真抢占 |

## 7. 项目结构

```
C:\Users\19444\Desktop\TuringComplete\
├── SymphonyPlus.isa
├── Reference\Linux-0.11-main\    # 内核源码（用户提供）
├── symcc\    编译器（C）：tokenize.c / parse.c / type.c / codegen.c / layout.c / preprocess.c
├── runtime\  运行时库（手写 .asm + C）
├── emu\      SymphonyPlus 指令模拟器（PC 端，开发期秒级验证）
├── tests\    chibicc 风格测试套件
├── kernel\   （M5 起）移植版内核源码
└── tools\    MINIX 镜像构建、磁盘文件生成
```

## 8. 风险与验证

| 风险 | 缓解 |
|---|---|
| 大内存模型复杂度 | M3 独立里程碑，游戏内超 64KB 程序验证 |
| 内核手术量（system_call/memory/fork/hd） | 隔离在 M5；M1–M4 不依赖；uClinux 先例 |
| 游戏内验证慢 | emu/ 模拟器承担开发期验证，真机最后 |
| 汇编器行为偏差（16 位越界、加载基址） | M0 实测；编译器输出保守（恒在 16 位内） |
| MINIX 镜像格式细节 | 由我们全权构建（PC 端 + 内核 fs 同源），无外部依赖 |

## 9. 未决事项（M0/M2 验证后定）
- 加载基址实测（假定 0；引导程序按编译器输出的绝对地址工作）
- a.out 或自定义可执行格式（M5 定）
- 屏幕驱动细节（光标、滚屏、颜色方案）以 ASCII 8 模式实现
