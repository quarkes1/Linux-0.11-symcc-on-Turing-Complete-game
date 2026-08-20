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
| **M2** 编译器特性 | ✅ 完成（编译+链接验收：Linux 0.11 内核 50 个 .c 编译链接通过） | 结构体/数组/位域、预处理（sympp）、.sym 可重定位对象、symld 链接器、gcc 式驱动、内核源码 SYMPLUS-PORT 编译演练 |

详细设计：`docs/superpowers/specs/2026-08-20-m2-compiler-design.md`、
`docs/superpowers/specs/2026-08-20-symcc-linux011-port-design.md`
实施计划：`docs/superpowers/plans/2026-08-20-m2-c89-compiler.md`

## 项目结构

```
TuringComplete/
├── symcc/                # C 交叉编译器（symcc.exe）
│   └── src/              # tokenize → preprocess → parse → codegen → obj
│                         # main.c 为 gcc 式驱动；compile.c 驱动单文件编译
├── symld/                # 链接器（symld.exe：.sym 对象 → 绝对地址 asm）
├── runtime/              # 运行时（crt0.asm 启动模板 + tty.c + divsi3.c）
├── emu/                  # 本地模拟器工具链（调试用，无需进游戏）
│   ├── isa.c             # 指令编码表（与 SymphonyPlus.isa 逐字一致）
│   ├── asm.c             # 两遍汇编器（asm.exe：asm → bin）
│   └── emu.c             # 指令模拟器（emu.exe：执行 + 退出码 + 内存断言）
├── tests/                # 端到端测试（make test：asm/emu/run/link/preproc 五套）
├── scripts/              # 构建脚本（build_kernel.sh：内核编译演练验收）
├── Reference/Linux-0.11-main/  # Linux 0.11 内核源码（SYMPLUS-PORT 适配）
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

`make test` 跑五个套件（全进程内，无子进程）：

1. `tests/asm_test.exe` — 汇编器单测（编码、伪指令展开、错误检测）
2. `tests/emu_test.exe` — 模拟器单测（ALU/跳转/load-store/screen 语义）
3. `tests/run_tests.exe` — 端到端：C → asm → bin → 执行 → 断言退出码与屏幕内容
4. `tests/preproc_test.exe` — 预处理器单测（宏展开/条件编译/include）
5. `tests/link_test.exe` — 对象格式与链接器单测（引用形态/多文件/重定位）

内核编译演练验收：

```bash
bash scripts/build_kernel.sh    # 50 个内核 .c → symcc → symld → kernel.asm
                                # 期望输出：KERNEL BUILD OK
```

## CLI 用法

`symcc.exe [选项] file1.c [file2.c ...]`

| 选项 | 作用 |
|---|---|
| `-E` | 只预处理，输出 .i 文本（stdout 或 `-o`） |
| `-S` | 编译到可重定位 asm（.sym 文本对象） |
| `-c` | 编译到 .sym 对象（同 -S 内容） |
| `-o <file>` | 输出文件（全链路默认 stdout） |
| `-I <dir>` | 头文件搜索路径（可多次） |
| `-D <name[=val]>` | 预定义宏（可多次） |
| `-save-temps` | 保留中间文件（.i/.s/.sym 于当前目录） |
| `--d32` | 数据引用强制 32 位装载（见[寻址约束](#寻址约束)） |
| `-v` | 打印各阶段文件名到 stderr |

**无选项 = 全链路**：每个文件 preprocess → compile obj → symld_link → 绝对 asm。
多文件链接自动（一个进程内完成，等效 `symcc -c` 各文件后 `symld *.sym`）。

`symld.exe file.sym [file.sym...] -o out.asm [--bin out.bin] [--crt0 path]`

## .sym 对象格式

每行一个指令/数据，段头行切换：

```
; symcc object v1
.text
    push r10
    mov r9, @g            ; 数据引用：D16（bss 低区直接）
    mov r9, @hi:h         ; 数据引用：D32 拆装（@hi + lsl 16 + or @lo）
    counter flags         ; call f（重定位：链接器决定桩化与否）
.data
g:  U32 0x00000005
.bss buf 100
.sym f T text 0 0
.sym g D data 0 4
```

- **引用形态**：`@name`=D16 绝对地址（仅低区 bss）；`@hi:name`/`@lo:name`=D32
  拆装（data 变量、extern、函数、字符串——链接布局在高区）；数据行
  `@32:name`=32 位地址指针。形态由 codegen 按符号类别自动选择，
  `--d32` 强制全部 D32
- **跳转重定位**：`call/jmp/条件跳转 name` 由链接器布局后解析；越界目标
  自动桩化/跳板化（见[寻址约束](#寻址约束)）
- **`.sym` 行**：`<name> <T|D> <text|data> <offset> <size>`；链接器重算
  text 符号偏移（桩化改变行大小），data/bss 符号按段累积

## 游戏内运行流程

1. 主机编译：`symcc/symcc.exe main.c -o out.asm`（或分步 `-c` + `symld`）
2. 复制 `out.asm` 全文
3. 游戏沙盒：主内存 RAM 右键 **"Edit assembly"** → 粘贴 → 运行
   （屏幕组件需链接到主内存；RAM 与 DiskA 均为 **Big endian**、8 MB）
4. 预期行为见程序（如 M1 验收：屏幕左上角显示 Hello）

## ABI 摘要

| 约定 | 值 |
|---|---|
| 端序 | 全链路大端（指令 + 数据；端序设置同时影响取指，必须大端） |
| 寄存器 | zr 恒 0；r1 表达式结果/返回值；r9 地址暂存；r10 帧指针；sp=14；flags=15 |
| 调用约定 | 全栈传参（实参右→左压栈，调用方清理）；返回值 r1；被调方序言 push r10 → mov r10,sp → sub sp,sp,frame |
| struct 实参 | 按大小 sub sp + 块拷贝入栈（非 4 字节对齐 push） |
| struct 返回 | 隐藏 arg0 = 返回缓冲区指针（[r10+8]），用户参数后移 |
| va_start | `__builtin_va_start` → AP = r10 + 8 + 4×固定参数（struct 返回函数再加缓冲区大小） |
| 栈 | 向下生长，crt0 设 sp = 0x4000（M1 阶段） |
| 伪指令 | push/pop/call/ret 为多词编码（8/8/20/12 字节，.isa 与 asm.c 一致；call 桩化后 24 字节） |
| 帧缓冲 | 屏幕 setting[0]=0（ASCII 8）、setting[1]=FRAMEBUF_BASE（0x2000）；96 列 × 40 行，行步长 96 |
| 退出码 | 程序 `halt`（jmp 自身）时 exit_code = r1 |

## 寻址约束

- **立即数 16 位**（≤0xFFFF）；>0xFFFF 常量拆 `mov hi; lsl 16; or lo` 拼接
- **跳转 J16 上限 64 KB**：`jmp/call/条件跳转` 的立即数形式只接受 0–0xFFFF
  绝对地址。链接器布局把 crt0 + 文字池 + 跳板 + bss 放在低区（<64 KB），
  对象代码在高区（>64 KB 允许）——越界目标自动处理：
  - `call sym` → 24 字节桩（存返回地址 → 池槽全 32 位装载 → jmp）
  - `jmp sym` → 8 字节桩（池槽装载 → jmp）；条件跳转 → 跳板
  - 池槽去重（同目标共享），数据引用不受影响
- **数据引用**：低区 bss → D16 直接；data/text（高区）→ 编译期 D32 拆装
  （`@hi:/@lo:`），`--d32` 强制全 D32
- **大内存模型属 M3**：16 位立即数寻址限 64 KB 直寻，大地址走寄存器寻址

## 关键设计决策

- **游戏汇编器 label 限制**：`jmp`/条件跳转/`call` 接受 label，`mov`/`load_*`/`store_*`
  的立即数只接受数字 → symcc 输出数据引用为绝对地址，控制流保留 label
- **伪指令多词编码**：push/call/ret 在游戏 .isa 中即多词定义，与本地汇编器展开
  逐字一致 → 两处布局完全相同，codegen 可可靠计算绝对地址
- **可重定位对象 + 链接器**：M2 起编译器输出 .sym 对象（引用保留符号名），
  symld 统一布局/解析/桩化 → 多文件编译与大型程序成为可能
- **M2 C 子集**：结构体/数组/位域/预处理/三元/逗号/自增自减/switch/goto/
  long long（32 位同宽）；未实现：浮点、goto 跨函数、真正的 long long 运算
- **内核 SYMPLUS-PORT**：x86 内联汇编（`__asm__`/寄存器绑定/语句表达式宏）以
  `SYMPLUS-PORT` 注释标记改纯 C 或 stub，M3 运行期语义验证
