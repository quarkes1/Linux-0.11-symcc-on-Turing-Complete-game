# M3 内核运行期验证设计（最小启动）

**日期**: 2026-08-21
**状态**: 设计已确认（brainstorming 完成，待实施计划）
**关联文档**: 2026-08-20-symcc-linux011-port-design.md（移植总体策略）、2026-08-20-m2-compiler-design.md

## 目标

Linux 0.11 内核在《Turing Complete》游戏内 SymphonyPlus CPU 上**真实运行起来**：
启动链各 init 阶段执行，printk 经控制台重定向输出到游戏屏幕，逐阶段打印进度
日志，最后在 `fork()` 前停机。游戏内可加载、可验证、不冻结游戏。

**范围外**（后续里程碑）：轮询 tick/调度（移植文档 M4）、fork/上下文切换、
直接调用式 syscall、信号（M4/M5）、磁盘驱动、shell 交互。

## 背景与现状

- M2 完成"编译+链接"验收：50 个内核 `.c` → symld → kernel.asm（12.5 万行，
  KERNEL BUILD OK）。运行期语义从未验证。
- 移植文档已定总体策略：`SYMPLUS-PORT` 注释标记每个改写点；轮询调度/tick
  薄层属 M4，本次不做。
- 2026-08-21 游戏内实测获得的关键事实：
  1. **屏幕协议**（游戏显示屏组件文档）：模式 0 = ASCII 8 文本 96×40；命令
     0 = 切换模式、1 = 数据偏移量、2 = 前景色、3 = 背景色（8 位色 RRRGGGBB）。
     crt0 已按此初始化（白字黑底，偏移 0x2000）。
  2. **加载方式**：RAM"外部文件"模式原样装载字节、不做汇编 → 内核验证必须
     走 `kernel.bin`（emu/asm.exe 产物，大端 32 位指令字）。**绝不粘贴**
     kernel.asm（3.4MB 文本会让游戏每次进沙盒重汇编而卡死——已踩坑）。
  3. 运行期自循环（jmp halt 类）不冻结游戏；冻结源于超大汇编文本重汇编。
  4. cmp 编码教训：模拟器测试全绿 ≠ 游戏 CPU 正确（见 emu/isa.c 1111 前缀
     修复，提交 7b1a7c7）；游戏内验收是最终标准。

## 设计

### 1. 数据流

```
游戏 RAM（kernel.bin，文件模式加载，地址 0 起）
  → crt0（屏幕 ASCII 8 + 白字黑底 + bss 清零 + call main，已完成）
  → main() 启动链逐阶段执行，每阶段 printk 一行日志
  → 日志经 printk → tty_write → console（重定向后）→ 帧缓冲 0x2000
  → fork() 前打印 boot banner → for(;;) 自循环停机
  → 屏幕显示完整启动序列（11 行）
```

### 2. 启动链与进度日志

| 阶段 | 现状 | M3 处理 | 日志行 |
|---|---|---|---|
| mem_init | 真实 C 代码 | 直接跑 | `mem_init OK` |
| trap_init | 门描述符全 stub | 保留 stub | `trap_init OK (stub)` |
| blk_dev_init | 真实代码 | 直接跑 | `blk_dev_init OK` |
| chr_dev_init | 真实代码 | 直接跑 | `chr_dev_init OK` |
| tty_init | 真实代码（console_init） | 直接跑 | `tty_init OK` |
| time_init | 8253 stub | 保留 stub（M3 不引入 tick） | `time_init OK (stub)` |
| sched_init | gdt 数据桩 | 保留 stub | `sched_init OK (stub)` |
| buffer_init | 真实代码 | 直接跑 | `buffer_init OK` |
| hd_init | 控制器探测 stub | 保留 stub | `hd_init OK (stub)` |
| floppy_init | 控制器探测 stub | 保留 stub | `floppy_init OK (stub)` |
| sti / move_to_user_mode | 空 stub | 保留（不打日志） | — |
| fork() | 未实现（M4/M5） | **停机** | `Boot complete. Halting.` |

日志插在 `init/main.c` 的 `main()` 各 init 调用之后，用现有 `printk`。

### 3. console 重定向（kernel/chr_drv/console.c）

现状：`con_init` 读 0x90000 BIOS 参数区（游戏里是空 RAM），`con_write` 按
VGA 文本模式写（每字符 2 字节：字符+属性），光标走 `outb_p` VGA 端口，
scrup/scrdown 按 VGA 几何滚屏。

改造（全部 `/* SYMPLUS-PORT: M3 ... */` 标记）：

1. **con_init**：不读 ORIG_VIDEO_* 参数；硬编码 `video_mem_start = 0x2000`
   （游戏帧缓冲基址）、`video_num_columns = 96`、`video_num_lines = 40`
   （与 symcc/include/config.h 的 FRAMEBUF_BASE/COLS/ROWS 一致；内核构建
   目前不引该头文件，先硬编码+注释，统一引用留待需要时）
2. **con_write**：逐字节写字符到帧缓冲，**丢弃属性字节**（游戏屏纯 ASCII，
   颜色由 crt0 的白字黑底设置提供）
3. **光标**：`outb_p` 调用全部空操作（游戏屏无光标寄存器）
4. **滚屏**：scrup/scrdown 内存搬移逻辑保留，作用于 0x2000 帧缓冲与 96×40
   几何

### 4. 停机点（init/main.c）

```c
    sti();
    move_to_user_mode();          /* 空 stub */
    /* SYMPLUS-PORT: M3 最小启动——fork/调度属 M4/M5，此处停机（自循环） */
    printk("\nBoot complete. Halting.\n");
    for (;;) ;
```

`for(;;)` 编译为 jmp 自身自循环；游戏 CPU 空转安全（见背景第 2 条）。

### 5. 构建管线（scripts/build_kernel.sh）

链接产出 kernel.asm 后追加：

```bash
../../emu/asm.exe ../../build/kernel.asm ../../build/kernel.bin
```

KERNEL BUILD OK 时 kernel.bin 一并产出（asm.exe 已修复 cmp 1111 前缀与
重复 label 容忍，见提交 7b1a7c7）。

### 6. 测试与验收

**模拟器端到端（自动化）**：新测试 `tests/kernel_boot_test.c`：
- 读 `build/kernel.bin` → `emu_run(bin, len, 8<<20, max_instr)`
- 断言 `res.error == 0` 且帧缓冲 `mem[0x2000]` 依次包含 11 行日志文本
  （mem_init OK … Boot complete. Halting.）
- max_instr 上限实施时实测（启动链指令数，取安全裕量）
- 挂进 Makefile 的 `test` 目标

**游戏内验收（人工）**：
- 文件模式加载 `build/kernel.bin` → 屏幕逐行显示启动日志 → 最后一行
  `Boot complete. Halting.` → 程序停在自循环
- 验收即通过

**沙盒安全红线**：只走 .bin 文件模式；任何情况下不得向游戏粘贴
kernel.asm（3.4MB 文本会让游戏重汇编卡死，需动存档文件才能恢复——
2026-08-21 已实际踩坑）。

## 约束

- 所有内核改动带 `/* SYMPLUS-PORT: ... */` 注释，git 可追溯
- 不改动 SymphonyPlus ISA 与游戏内 CPU 电路
- 不引入 tick/调度/syscall（M4 范围），本设计为它们预留接口即可
- 工具链不改动（console 重定向纯 C 实现）

## 验收标准

1. `mingw32-make test` 全绿（含新的 kernel_boot_test）
2. `bash scripts/build_kernel.sh` 输出 KERNEL BUILD OK 且产出 kernel.bin
3. 游戏内文件模式加载 kernel.bin：屏幕显示完整 11 行启动日志后停机，
   游戏 UI 不冻结

## 任务分解预览

1. console.c 重定向（con_init/con_write/滚屏/光标）
2. init/main.c 进度日志 + 停机点
3. build_kernel.sh 产出 kernel.bin
4. tests/kernel_boot_test.c + make test 挂载
5. 游戏内验收 + README 里程碑更新
