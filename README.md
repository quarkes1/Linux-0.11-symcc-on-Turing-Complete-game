# SymphonyPlus — Turing Complete 游戏内 32 位 CPU 工具链

在《Turing Complete》游戏的自制 32 位 CPU **SymphonyPlus** 上运行 C 程序，
最终目标：**移植并运行 Linux 0.11**。

- **CPU**：32 位定长指令，16 个寄存器（zr/r1–r13/sp/flags），大端全链路
- **工具链**：全部 C 语言实现（Windows / mingw 构建）
- **硬件**：主内存 8 MB + DiskA 8 MB（Big endian），屏幕（ASCII 8，96×40）
  与键盘组件，基址约定见 [ABI](#abi-摘要)

## 里程碑状态

| 里程碑 | 状态 | 内容 |
|---|---|---|
| **M0** 环境验证 | ✅ 完成（部分项待游戏内复核） | 加载基址、RAM/DiskA 容量（8MB）、帧缓冲协议、端序（大端） |
| **M1** 编译器骨架 | ✅ 完成（游戏内验收通过：屏幕显示 Hello） | symcc 单遍编译器、模拟器、屏幕运行时、有符号除/模 |
| **M2** 编译器特性 | ⬜ 待办 | 结构体、数组、预处理、更多 C 子集，为 Linux 0.11 移植准备 |

详细设计：`docs/superpowers/specs/2026-08-20-symcc-linux011-port-design.md`
实施计划：`docs/superpowers/plans/2026-08-20-symcc-m0-m1.md`

## 项目结构

```
TuringComplete/
├── symcc/                # C 交叉编译器（symcc.exe）
│   ├── src/              # tokenize → parse → codegen（单遍，M1 子集）
│   └── include/config.h  # 硬件约定（FRAMEBUF_BASE、COLS/ROWS）
├── runtime/              # 运行时库（C 源码，随程序编译）
│   ├── crt0.asm          # 启动代码模板（codegen 拼接，FRAMEBUF_BASE 占位符替换）
│   ├── tty.c             # putchar/putstr 屏幕输出（帧缓冲直写）
│   └── divsi3.c          # 有符号除/模（__divsi3/__modsi3）
├── emu/                  # 本地模拟器工具链（调试用，无需进游戏）
│   ├── isa.c             # 指令编码表（与 SymphonyPlus.isa 逐字一致）
│   ├── asm.c             # 两遍汇编器（asm.exe：asm → bin）
│   └── emu.c             # 指令模拟器（emu.exe：执行 + 退出码 + 内存断言）
├── tests/                # 端到端测试（make test）
│   ├── run_tests.c       # C→asm→bin→emu 全链路断言（9 个测试）
│   └── test_*.c          # 测试用例
└── SymphonyPlus.isa      # 游戏指令集定义（游戏内导入，与 emu/isa.c 同步）
```

## 构建

```bash
# Git Bash 下需 MSYS2 mingw 前缀（DLL 冲突，见 LEARNINGS.md）
export PATH="/d/Downloads/msys64/mingw64/bin:$PATH"
mingw32-make              # 构建全部工具
mingw32-make test         # 运行全部测试
```

## 测试

`make test` 跑三个套件（全进程内，无子进程）：

1. `tests/asm_test.exe` — 汇编器单测（编码、伪指令展开、错误检测）
2. `tests/emu_test.exe` — 模拟器单测（ALU/跳转/load-store/screen 语义）
3. `tests/run_tests.exe` — 端到端：C → asm → bin → 执行 → 断言退出码与屏幕内容

## 游戏内运行流程

1. 主机编译：`symcc/symcc.exe main.c runtime/tty.c runtime/divsi3.c -o out.asm`
2. 复制 `out.asm` 全文
3. 游戏沙盒：主内存 RAM 右键 **"Edit assembly"** → 粘贴 → 运行
   （屏幕组件需链接到主内存；RAM 与 DiskA 均为 **Big endian**、8 MB）
4. 预期行为见程序（如 M1 验收：屏幕左上角显示 Hello）

## ABI 摘要

| 约定 | 值 |
|---|---|
| 端序 | 全链路大端（指令 + 数据；端序设置同时影响取指，必须大端） |
| 寄存器 | zr 恒 0；r1 表达式结果/返回值；r9 地址暂存；r10 帧指针；sp=14；flags=15 |
| 调用约定 | 全栈传参（实参右→左 push，调用方清理）；返回值 r1；被调方序言 push r10 → mov r10,sp → sub sp,sp,frame |
| 栈 | 向下生长，crt0 设 sp = 0x4000（M1 阶段） |
| 伪指令 | push/pop/call/ret 为多词编码（8/8/20/12 字节，.isa 与 asm.c 一致） |
| 帧缓冲 | 屏幕 setting[0]=0（ASCII 8）、setting[1]=FRAMEBUF_BASE（0x2000）；96 列 × 40 行，行步长 96 |
| 立即数 | 16 位（≤0xFFFF）；>0xFFFF 常量拆 mov hi; lsl 16; or lo 拼接；数据地址由 codegen 布局 pass 解析为绝对地址（游戏汇编器数据位置不接受 label） |
| 退出码 | 程序 `halt`（jmp 自身）时 exit_code = r1 |

## 关键设计决策

- **游戏汇编器 label 限制**：`jmp`/条件跳转/`call` 接受 label，`mov`/`load_*`/`store_*`
  的立即数只接受数字 → symcc 输出数据引用为绝对地址，控制流保留 label
- **伪指令多词编码**：push/call/ret 在游戏 .isa 中即多词定义，与本地汇编器展开
  逐字一致 → 两处布局完全相同，codegen 可可靠计算绝对地址
- **M1 C 子集**：无预处理器、无数组下标（`*(p+i)`）、无类型转换（全局指针初始化
  代替）、无后缀 ++/--、无三元、无原型声明（预扫描全局注册）、单声明器
- **大内存模型属 M3**：16 位立即数寻址限 64 KB 直寻，大地址走寄存器寻址
