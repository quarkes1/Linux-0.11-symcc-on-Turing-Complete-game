# fork、DiskA 文件系统与交互 shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 启动 Linux 0.11 移植内核后，在游戏屏幕上使用真实键盘操作 shell，经 fork/exec/wait 执行 DiskA MINIX 文件系统中的独立程序。

**Architecture:** 用户进程共用固定的 512KB 活动窗口，每个进程有独立 RAM 快照；合作式调度仅在系统调用边界切换。内核、任务元数据、磁盘缓存常驻。沿用 Linux task/current、文件引用、块缓存和 MINIX 文件系统，替换依赖 x86 页表、TSS、中断及 BIOS 的平台路径。

**Tech Stack:** C（symcc 目标子集与宿主工具）、SymphonyPlus 汇编、Python 3 镜像工具、MSYS2 GCC/mingw32-make、Git Bash。

**Spec:** `docs/superpowers/specs/2026-10-17-fork-disk-shell-design.md`（2026-10-17 用户已批准）。

## Global Constraints

- 保留现有 CPU 电路与 ISA、8MB RAM、8MB DiskA、大端指令和数据、96×40 文本屏幕。
- 无 MMU、无硬件中断，采用合作式调度。游戏内最终验收不能由模拟器测试替代。
- PID 0 是常驻内核 idle，不占用户快照；最多 8 个用户进程，包含 init/shell。
- 0x200000…0x27ffff 为唯一活动用户窗口；0x400000…0x7fffff 为 8 份 512KB 用户快照。
- 内核和进程管理元数据始终常驻，不进入用户快照。
- 原有 `emu_run` 和 `symld_link` 接口保留，原测试仍可独立运行。
- 首版包含磁盘文件系统、ls/cat 和外部程序加载。
- 内核适配继续标注 SYMPLUS-PORT；没有游戏操作与用户实测证据时不能声称游戏验收通过。
- 按已跟踪源码显式列举构建输入，禁止混入 build 中残留的旧对象。

## Review Focus

1. 高地址用户程序的取反分支及函数指针调用：目标与返回地址都必须准确，不破坏比较结果（任务 2）。
2. 内存窗口尾部地址和长度回绕：系统调用必须拒绝越界访问，不能触及快照或内核（任务 4、8）。
3. 同一进程反复 fork/exit/wait：资源不能泄漏，其他进程的 zombie 不得被错误回收（任务 6）。
4. 磁盘短读、损坏 zone 和写请求失败：解锁缓存并保留可继续运行的 shell（任务 7、8）。
5. 输入在子进程运行或终端缓冲满时到达：事件不能变成重复命令，缓冲不能越界（任务 9、11）。

## 文件与接口分工

| 文件/目录 | 职责 |
| --- | --- |
| `runtime/platform.h` | 主机工具与目标运行时共用的数值 ABI：窗口、网关、镜像头、上下文槽 |
| `symcc/src/link.c`, `link.h`, `symld/symld.c` | 兼容旧链接入口，增加内核/用户布局、32 位分支与显式保留区 |
| `runtime/platform.sym` | 手写、可跟踪的可重定位汇编桥接对象；沿用现有文本 .sym 格式 |
| `emu/emu.c`, `emu.h`, `emu_main.c` | 可配置 I/O、确定性时钟、键盘事件、镜像读写和交互运行 |
| `Reference/Linux-0.11-main/include/linux/symplus.h` | 内核平台 C 接口，引入共用 ABI |
| `Reference/Linux-0.11-main/kernel/symplus/` | 常驻上下文、快照、用户检查、平台系统调用、键盘与时钟 |
| `Reference/Linux-0.11-main/kernel/{fork,sched,exit}.c` | Linux 进程生命周期的平台分支 |
| `Reference/Linux-0.11-main/kernel/blk_drv/symdisk.c` | DiskA 同步块设备请求 |
| `Reference/Linux-0.11-main/fs/symexec.c`, `minix_disk.c` | 平坦可执行文件加载和磁盘结构显式编解码 |
| `user/include/`, `user/lib/`, `user/bin/` | 用户头文件、最小 libc、独立用户程序 |
| `scripts/mkrootfs.py`, `scripts/build_user.sh` | 可重现的大端 MINIX 镜像与用户构建 |
| `tests/` | 宿主单测和在模拟器上执行目标机器码的验收程序 |

## 构建与验证约定

PowerShell 中统一使用已确认存在的 UCRT64 工具目录：

```powershell
$env:PATH = 'D:\Downloads\msys64\ucrt64\bin;' + $env:PATH
mingw32-make all
mingw32-make test
```

先重建基线；旧 exe 的通过不代表当前源码已重建。新测试添加到 `all`/`test`。
独立目标统一用 `mingw32-make tests/<name>.exe` 构建，再执行对应 exe。
每任务先观察预期行为测试失败，再实现；禁止通过放宽指令上限、伪造输出或跳过客户机路径使测试变绿。
每个任务验证通过后记录完成复选框与命令结果，再提交该任务涉及的文件；不把未验证功能写成完成。

### Task 1: 重建基线与平台内存合同

**Files:** 创建 `runtime/platform.h`、`tests/platform_test.c`；修改 `Makefile`、`scripts/build_kernel.sh`、`.gitignore`。

**Interfaces:** 产生 `SP_USER_BASE=0x200000u`、`SP_USER_SIZE=0x80000u`、`SP_SNAPSHOT_BASE=0x400000u`、`SP_MAX_USERS=8u`、`SP_BRANCH_GATE=0x100u`、`SP_SYSCALL_GATE=0x104u`。网关区域 `[0x100,0x140)` 与帧缓冲 `[0x2000,0x2f00)` 均为保留区。上下文 `SpContext` 包含 `unsigned regs[16]; unsigned pc;`，r0 恒为零，r14=sp，r15=flags。目标 unsigned 必须为 32 位。

- [ ] 重建六套原测试并运行，记录源码基线；失败时先定位并报告，不能继续声称基线为绿。
- [ ] 写失败测试固定区域不重叠、槽位总长度及上下文偏移：
  ```c
  assert(SP_USER_BASE + SP_USER_SIZE <= SP_SNAPSHOT_BASE);
  assert(SP_SNAPSHOT_BASE + SP_MAX_USERS * SP_USER_SIZE == 8u * 1024u * 1024u);
  assert(sizeof(((SpContext *)0)->regs[0]) == 4);
  assert(offsetof(SpContext, pc) == 64);
  ```
- [ ] 构建并运行 `tests/platform_test.exe`，确认因缺少合同定义失败；添加共用定义后运行通过。
- [ ] `.gitignore` 对 `runtime/platform.sym` 及用户 crt0 源对象添加精确例外；构建脚本用工具探测或外部 PATH，移除硬编码 mingw64 前缀覆盖。禁止更改原 clean 含义或删除工作区外内容。
- [ ] 为内核构建产生明确对象清单，区分 `build/m3/` 和 `build/interactive/`，同名头/源变化触发重建；测试修改头文件后能更新产物。
- [ ] 运行 `mingw32-make test`，提交已验证基线与平台合同。

### Task 2: 无重叠布局、32 位控制流与用户链接

**Files:** 修改 `symcc/src/link.c`、`link.h`、`symld/symld.c`；创建 `symcc/src/link_layout.c`、`link_layout.h`、`tests/user_link_test.c`；扩展 `tests/link_test.c`、`Makefile`。

**Interfaces:** 新增 `LinkOptions { unsigned base, limit, branch_gate, syscall_gate; int image_kind; }`；`image_kind` 取 `SP_IMAGE_LEGACY`、`SP_IMAGE_KERNEL`、`SP_IMAGE_USER`。新增 `bool symld_link_ex(Obj **objs,int n,const char *crt0,const LinkOptions *opt,FILE *out_asm,FILE *out_bin,LinkError *err)`；原入口调用 legacy 配置。

- [ ] 写回归：生成足量不同分支目标，让旧地址池穿越 `0x2000`；执行清屏后逐个分支，必须均返回正确结果。检查链接映像清单中任何代码/池/BSS 都不占帧缓冲。
- [ ] 写用户链接测试，基址 `SP_USER_BASE`；主程序跨越 64KB，混合十种有符号/无符号条件、前后跳、递归、函数指针及静态数据地址。旧接口不支持用户布局，应失败。
- [ ] 在 linker 中统一计算扩展后行长。新模式的控制流模板固定如下，数据引用使用 `@hi/@lo/@32`：
  ```text
  conditional: mov r13,HI(target); lsl r13,r13,16; or r13,r13,LO(target); jCC SP_BRANCH_GATE
  jump:        mov r13,HI(target); lsl r13,r13,16; or r13,r13,LO(target); jmp r13
  call:        counter flags; add flags,flags,32; sub sp,sp,4; store_32 [sp],flags;
               mov r13,HI(target); lsl r13,r13,16; or r13,r13,LO(target); jmp r13
  ```
  条件模板只写 r13；共用低区网关仅执行 `jmp r13`。用户镜像不包含也不覆盖内核网关。
- [ ] 内核入口在最前面先跳过保留网关，布局器显式跳过两段保留区；新模式 BSS 使用高地址和 32 位引用。验证镜像、启动栈、缓存分区均在 2MB 内且互不重叠；超出时报错。
- [ ] `symld --image kernel|user` 输出用户镜像时不填充从地址 0 到基址的 2MB 零区；绝对汇编与紧凑 bin 的坐标转换由链接器统一处理，不能截掉有效字节。
- [ ] 窗口或符号溢出必须失败；运行 `user_link_test`、原链接/编译/启动测试后提交。

### Task 3: 模拟器的磁盘、键盘、确定性时间与可观察停止

**Files:** 修改 `emu/emu.h`、`emu/emu.c`；创建 `tests/emu_io_test.c`；修改 `Makefile`。

**Interfaces:** 新增 `EmuEvent { uint64_t at_instruction; uint32_t key; }` 和 `EmuOptions { const uint8_t *disk; size_t disk_len; const EmuEvent *events; size_t n_events; uint64_t ns_per_instruction; int (*observe)(const EmuResult *,uint64_t,void *); void *opaque; }`；`EmuResult emu_run_ex(const uint8_t *,size_t,size_t,uint64_t,const EmuOptions *)`。结果增加执行指令数及停止原因枚举（halt、observer、limit、fault）；旧 error 值保持兼容。

- [ ] 测试用汇编执行真实 keyboard、time_0/time_1、pload/pstore；原实现读取不到输入和镜像内容，观察失败。
- [ ] 分批注入按下/释放事件，尚未到时不能提前读出；空队列返回 0。低时钟读取锁存高位，测试 32 位回绕。
- [ ] 执行器初始化 8MB 磁盘并复制输入镜像，镜像过大拒绝；写只修改结果内存。边界 `size-4` 可读写，`size-3` 的 32 位操作报错。
- [ ] 观测钩子只读客户机状态并可结束测试；不改变 PC、不解释命令。旧 `emu_run` 使用空配置，保持原测试行为。
- [ ] 测试 `screen` 命令号越界不能越过 settings 数组；正常设置与帧缓冲协议不变。
- [ ] 运行模拟器全部测试及源码基线，通过后提交。

### Task 4: 汇编桥、用户指针与系统调用入口

**Files:** 创建 `runtime/platform.sym`、内核 `include/linux/symplus.h`、`kernel/symplus/{context,syscall,usercopy}.c`、`tests/context_test.c`；修改用户/内核链接配置及 `Makefile`。

**Interfaces:** `int sp_user_range(unsigned addr,unsigned len)`、`int sp_copy_string(char *dst,unsigned cap,const char *src)`；`SpContext *sp_dispatch(SpContext *saved)` 是汇编桥调用的唯一 C 调度入口；`void sp_resume(SpContext *next)` 不返回。系统调用号放 r1，最多三个参数放 r2…r4，结果放 r1；用户包装将现有栈 ABI 的参数搬入指定寄存器。

- [ ] 通过目标机器码测试上下文往返：寄存器含不同哨兵、栈上含链式指针，调用后数值和返回点完全正确。模拟器不得识别函数名或额外实现上下文切换。
- [ ] 汇编入口先在原用户栈保存完整现场，再转到常驻临时栈，把现场复制到常驻上下文；C 层不能接收会在窗口切换后失效的 saved 指针。恢复过程反向操作，最后恢复 sp、返回 PC。
- [ ] 只允许在调用边界切换，flags 作为 ABI scratch 保存原始值；不承诺在任意 cmp 与分支之间抢占，避免依赖未确认的 flags 位布局。
- [ ] 用户范围检查用减法防回绕：
  ```c
  return addr >= SP_USER_BASE && addr <= SP_USER_BASE + SP_USER_SIZE &&
         len <= SP_USER_BASE + SP_USER_SIZE - addr;
  ```
  零长度可接受窗口结束地址，非零长度不可；字符串必须在界内遇到 NUL。
- [ ] 测试 `end-1,len=2`、`0xfffffff0,len=64`、跨界 argv 指针、无 NUL 字符串均返回 EFAULT/ENAMETOOLONG，内核哨兵不变。
- [ ] 暂时只接通 getpid、write、ENOSYS 及测试 yield；运行桥接和长地址测试后提交。

### Task 5: Linux task/current 与固定窗口调度

**Files:** 创建 `kernel/symplus/{process,memory}.c`；修改 `include/linux/sched.h`、`kernel/sched.c`、`mm/memory.c`；创建 `tests/process_test.c`。

**Interfaces:** `int sp_process_init(void)`、`int sp_schedule(void)`、`int sp_snapshot_save(int slot)`、`int sp_snapshot_restore(int slot)`；平台元数据含 `SpContext context; unsigned snapshot; int slot;`，与 `task[]` 一一对应，不能另建语义独立的进程表。槽 1…8 对应用户任务，内核 task[0] 常驻。

- [ ] 手工建立两个目标进程，上下文均指向活动窗口相同地址；全局值和栈指针在至少 20 次 yield 后保持各自数值，原空 switch_to 必须失败。
- [ ] 快照使用固定 RAM，数据复制循环在常驻栈上运行；记录活动槽，只有活动任务可保存。上下文和 snapshot 元数据不可位于用户窗口。
- [ ] 以 `TASK_RUNNING` 和等待状态实现轮转，更新 Linux current；syscall 在内核栈运行完再选择恢复上下文，不在普通 C 栈中途跳入另一任务。
- [ ] 新平台配置的内存分配仅管理明确预留的常驻区；阻断旧页表路径写入地址 0。为 buffer cache 提供独立边界，避免 `&end` 或 BIOS 值参与定位。
- [ ] 无可运行用户任务时回到 idle 轮询入口；不把无任务视为 halt 或指令错误。测试阻塞任务不会被运行。
- [ ] 验证原 M3 配置仍可构建通过，提交窗口调度。

### Task 6: fork、exit、waitpid 与资源回收

**Files:** 修改 `kernel/fork.c`、`kernel/exit.c`、`kernel/symplus/process.c`、`kernel/symplus/syscall.c`；创建 `tests/fork_test.c` 及目标程序 `tests/fixtures/fork_probe.c`。

**Interfaces:** `int sp_fork(SpContext *ctx)`、`int sp_waitpid(int pid,unsigned *status,int options)`、`void sp_exit(int code)`；返回内核负 errno，用户 libc 才转换。exec/read 等可能阻塞的 syscall 通过任务的 pending 状态在恢复用户之前完成，不能依赖已被覆盖的用户栈或普通 C 调用栈。

- [ ] 目标探针固定父子行为：
  ```c
  int global = 7;
  int main(void) {
      int local = 9, *p = &local, status, pid = fork();
      if (pid < 0) return 10;
      if (pid == 0) { global = 20; *p = 30; _exit(42); }
      if (waitpid(pid, &status, 0) != pid) return 11;
      return global == 7 && *p == 9 && status == (42 << 8) ? 0 : 12;
  }
  ```
- [ ] 运行该探针确认旧 sys_fork 失败；复制窗口和现场，子 r1=0，父 r1=新 PID，更新 father 和引用计数，分配失败回滚。
- [ ] exit 关闭文件引用、释放 cwd/root/executable、标记 zombie、唤醒指定父任务；不再调用旧 free_page_tables。wait 支持指定 PID、-1、WNOHANG 和 ECHILD。
- [ ] 顺序创建回收至少 64 次；并发填满 8 用户槽，检查 EAGAIN 后父进程仍可写屏、等待并再次 fork。
- [ ] 验证一个父任务不能回收另一父任务的子任务；孤儿归 PID 1，并由 init 回收；共享打开文件偏移的完整测试在任务 8 接入后运行。
- [ ] 运行窗口、fork、原全部回归测试后提交。

### Task 7: DiskA 块驱动与标准 MINIX 磁盘结构

**Files:** 创建 `kernel/blk_drv/symdisk.c`、`fs/minix_disk.c`、`scripts/mkrootfs.py`、`tests/disk_test.c`、`tests/test_mkrootfs.py`；修改 `fs/{inode,super,buffer}.c`、`include/linux/fs.h`、`kernel/symplus/syscall.c`、构建脚本。

**Interfaces:** `unsigned sp_disk_read32(unsigned addr)`、`void sp_disk_write32(unsigned addr,unsigned value)` 由汇编桥提供；`void symdisk_init(void)` 注册 major 3/minor 0。磁盘序列化提供 `sp_get_be16/32`、`sp_put_be16/32`；Python 对应使用 `struct.pack_into('>H',...)` / `'>I'`。

- [ ] 宿主镜像测试固定字节序和偏移，不能只做自身读写往返：
  ```python
  disk = make_image({'/etc/motd': b'hello\n'})
  assert disk[1024 + 16:1024 + 18] == b'\x13\x7f'
  assert len(disk) == 8 * 1024 * 1024
  assert DISK_INODE_BYTES == 32 and DIR_ENTRY_BYTES == 16
  ```
  `make_image(files: dict[str,bytes]) -> bytes` 在 `scripts/mkrootfs.py` 定义，创建目录和设备节点。
- [ ] 目标程序通过真实 pload/pstore 和 Linux bread/ll_rw_block 读取已知块、写入另一块，检查结果磁盘字节；旧硬盘路径不工作，应失败。
- [ ] 同步处理 request 链，检查 sector 和 sector_count 的容量/溢出；成功和失败均释放请求、解锁 bh、唤醒等待者，失败不得解引用空 bh。
- [ ] 审核 inode/super 布局并显式编解码 32 字节 inode、18 字节磁盘 super 字段与 16 字节目录项；内存 struct 布局不决定盘上布局。支持直接区、单间接区和双间接区，检查 zone 范围。
- [ ] 挂载 major3/minor0 根设备不执行 x86 分区/BIOS 探测。固定缓存范围，真实调用 mount_root、iget、namei。
- [ ] 损坏 magic、越界 zone、最后块边界、非法设备号与失败写请求均有机器码测试，失败后仍能完成下一有效读请求。
- [ ] 运行 `python -m unittest discover -s tests -p test_mkrootfs.py`、`disk_test` 及回归，通过后提交。

### Task 8: 文件 syscall 与事务式 exec

**Files:** 创建 `fs/symexec.c`、`tests/exec_test.c`、`tests/file_test.c`；修改 `fs/{read_write,open,exec}.c`、`kernel/symplus/syscall.c`、`runtime/platform.h`。

**Interfaces:** `int sp_execve(const char *path,char **argv,char **envp,SpContext *ctx)`；镜像头为 7 个大端 U32：magic `0x53505831`（SPX1）、版本1、入口偏移、装载字节数、BSS字节数、栈保留字节数、flags=0。入口相对 `SP_USER_BASE`；磁盘文件长度=28+装载字节数。默认保留 64KB 栈，argc/envc 各最多 32，参数字符串合计最多 8192 字节。

- [ ] 文件测试通过目标 syscall 实际创建 `/tmp/probe`、写读、lseek、dup、stat、chdir 和列目录。fork 继承打开文件描述并共享偏移；关闭子 fd 不关闭父 fd。
- [ ] 正常 exec 运行不同程序，检查 PID 不变、BSS 清零、argc/argv/envp 正确；失败 errno 覆盖 ENOENT、ENOEXEC、E2BIG、EFAULT、ENOMEM。
- [ ] 完整读取映像到当前进程的备用快照区并检查全部长度；参数先复制到常驻缓冲。验证成功后才提交窗口替换和上下文；失败不能改活动窗口或旧返回点。
- [ ] 生成初始 C ABI 参数栈，用户 crt0 调用 `main(argc,argv,envp)` 后调用 exit；执行文件指针和 close_on_exec 引用在提交点更新。
- [ ] 目标测试证明坏 magic、入口越界、整数溢出、截断头、截断正文、超长参数和文件中途读错之后，旧程序仍可 write、fork、exit。
- [ ] sync 并用结果磁盘重新启动客户机，检查 `/tmp/probe` 内容和元数据持久化；不得只检查缓存。
- [ ] 运行 file/exec/fork/disk 测试与原回归，通过后提交。

### Task 9: 键盘轮询、TTY 行规程和时钟

**Files:** 创建 `kernel/symplus/{keyboard,tick}.c`、`tests/tty_input_test.c`；修改 `kernel/chr_drv/{tty_io,console}.c`、汇编桥和 syscall 分派。

**Interfaces:** `unsigned sp_keyboard_event(void)`、`unsigned sp_time_low(void)`、`unsigned sp_time_high(void)`；`void sp_poll(void)` 推进键盘和时钟；终端 read 使用 Linux tty 队列，未成行时记录 pending read 并调度，不在可换出的 C 栈上 sleep。

- [ ] 定时注入按下/释放、Shift、大小写、数字、空格、斜线、Enter、Backspace，断言真实 read 返回字符序列，释放事件不能输出字符。
- [ ] 1024 字节行队列满时丢弃超出字符并维持可恢复状态；Enter 仍提交已接受的内容，回显只反映已接受内容。测试多行突发输入不串行、不重复。
- [ ] read 的用户目标缓冲只在目标任务窗口活动时复制；其他任务活动时保留常驻 pending 元数据，禁止提前写入同地址的别的任务。
- [ ] console 滚屏超过 100 行，检查最后屏幕行与帧缓冲前后哨兵；CR/LF/退格行为统一到当前字节单元几何。
- [ ] tick 由锁存后的 64 位时间差推进，测试低位回绕及一次轮询跨过多个 tick；没有 runnable 任务时仍轮询。
- [ ] 通过 TTY、窗口和 fork 测试后提交；硬件键码映射在最终游戏验收中单独记录。

### Task 10: 用户 libc、init、sh 与独立命令

**Files:** 创建 `user/include/{unistd,stdio,stdlib,string,dirent,syscall}.h`、`user/lib/{crt0.sym,syscall.sym,libc.c}`、`user/bin/{init,sh,ls,cat,echo,forktest}.c`、`scripts/build_user.sh`、`tests/user_program_test.c`；修改 `.gitignore` 和镜像工具。

**Interfaces:** libc 的 fork/execve/waitpid/read/write/open/close 等与已有 C 原型一致；负 errno 转换为 `errno` 和 -1。目录枚举封装读取盘上 16 字节目录项，ls 使用 stat 区分目录/文件。`user_program_test` 从磁盘加载并真实执行每个程序。

- [ ] 首先构建独立 echo 并 exec，测试 `echo one two` 输出 `one two\n` 且退出0；写循环必须处理短写和错误。
- [ ] libc 提供有界字符串、十进制打印、简单分配器与需要的 fd 包装，不引入宿主 libc 到目标镜像。syscall stub 用现有栈参数 ABI 装载 r1…r4 后进入固定网关。
- [ ] init 作为 PID1 打开 `/dev/tty0` 并 dup 为0/1/2，fork/exec `/bin/sh`，wait 回收并在 shell 退出后重启；也回收孤儿。
- [ ] sh 使用 256 字节命令行、最多32个词，提示符 `symplus$ `；内建 cd/exit，其他命令按绝对/相对路径或 `/bin/` 搜索后 fork/exec/wait。未知命令打印诊断，子退出127，父继续提示符。
- [ ] ls 支持无参数和单路径；cat 循环读取多个块并处理不存在文件；echo 输出参数；forktest 检查父子内存隔离和 wait 状态并打印 PASS。
- [ ] 测试 /bin 文件确实是各自独立 SPX1 文件；删掉 `/bin/cat` 后 cat 必须报找不到程序，不能回退内核内建命令。
- [ ] 打包根目录、/bin、/dev、/etc/motd、/tmp，执行用户程序及文件系统回归后提交。

### Task 11: 交互启动、可复现构建与完整客户机验收

**Files:** 修改 `init/main.c`、`scripts/build_kernel.sh`、`Makefile`、`emu/emu_main.c`、`tests/kernel_boot_test.c`、`README.md`；创建 `tests/shell_test.c`、`scripts/build_system.sh`。

**Interfaces:** `bash scripts/build_system.sh` 构建 `build/kernel.bin` 和 `build/rootfs.bin`；`bash scripts/build_kernel.sh --m3` 构建 `build/m3/kernel.bin` 供旧停机验收。`emu.exe build/kernel.bin --disk build/rootfs.bin --interactive` 运行交互；只有显式 `--write-disk <path>` 才把结果写回宿主文件。

- [ ] 集成测试先启动真实客户机，观察首次提示符后才注入键盘事件，依次执行：
  ```text
  ls /bin
  cat /etc/motd
  echo hello
  forktest
  nosuchcommand
  cd /etc
  cat motd
  exit
  ```
  断言各命令输出、forktest PASS、错误后的提示符及 shell 重启后的新提示符；宿主不替客户机执行命令。
- [ ] 内核 main 使用新平台内存初始化、磁盘注册、挂载、TTY/tick、加载 PID1；保留 M3 宏配置。移除交互配置对 BIOS、x86设备探测和旧页表初始化的调用，启动失败打印具体阶段并安全停机。
- [ ] CLI 将终端输入转换为键盘事件并显示客户机屏幕更新；EOF 后正常结束会话，绝不将满指令预算误报为正常 halt。实时输入实现使用宿主可用的标准运行时接口，不引入 Windows SDK 依赖。
- [ ] 构建依赖包含所有内核源、头、汇编源、用户程序和打包脚本；头文件修改后 `make test` 必须刷新镜像。rootfs 打包失败必须终止构建，不能复用旧镜像假通过。
- [ ] 另测 100 次命令、子进程运行期间分批输入、长行退格、屏幕滚动、错误磁盘和丢失 init；测试以 observer 正常结束，不能用 halt 代替交互等待。
- [ ] 执行完整命令：
  ```powershell
  mingw32-make all
  bash scripts/build_system.sh
  mingw32-make test
  python -m unittest discover -s tests -p test_mkrootfs.py
  ```
  逐项记录结果；列出任何已观察到的失败，包括非本任务引入的失败。
- [ ] 更新 README：两个镜像加载位置、键盘聚焦、启动提示符、命令列表、内存/进程限制、模拟器运行和写盘选项。不得把“编译通过”写作“游戏验收通过”。提交经过验证的交互系统。

### Task 12: 完整变更审查与游戏验收

**Files:** 审查所有变更；在本计划末尾添加验收记录；根据发现修改责任文件。

- [ ] 独立审查覆盖共享 ABI、栈窗口切换、syscall 用户指针、exec 失败原子性、磁盘序列化、请求锁、文件引用和构建陈旧产物。只有选定执行方式或技能明确要求后才派出审查代理。
- [ ] 每个实质缺陷先加复现，再修复并重跑对应测试及完整测试；不为仅样式问题扩大重构。
- [ ] 交付确实存在的 kernel.bin/rootfs.bin 及构建方法；若此环境无法操作游戏，则明确标记“模拟器验收通过，游戏待实测”，提供用户可直接执行的加载步骤。
- [ ] 游戏验收：RAM 外部文件加载 kernel.bin；DiskA 外部文件加载 rootfs.bin；均大端、8MB。屏幕出现提示符，实际键盘执行上述命令并检查大小写、退格、滚屏和再次提示符。
- [ ] 只有全部要求都有证据时将整体任务标为完成；没有游戏证据不得宣称游戏通过。

## 计划自审结果

- 规格的内存/ABI、fork/wait、DiskA/MINIX、exec、键盘/TTY、用户命令、模拟器、构建和游戏验收均有责任任务。
- 五项 Review Focus 已分别加入任务 2、4/8、6、7/8、9/11 的失败用例。
- 保留旧入口以控制回归范围；新接口均在首次生产任务定义，后续任务按相同签名消费。
- 真实用户进程、外部文件和磁盘路径是测试要求，未以宿主模拟命令或内核内建命令替代。
- 当前仅完成设计和计划文件；所有实现复选框保持未勾选。

## 计划确认前的源码基线验证（2026-10-17）

- `mingw32-make -B all` 成功，全部宿主工具和测试由当前源码重新生成，输出未出现编译警告。
- 重新生成的 asm_test、emu_test、run_tests、preproc_test、link_test 全通过；其中编译器端到端 29 项、预处理器 30 项、链接器 48 项。
- 使用重新生成的 symcc/symld/asm 执行 `bash scripts/build_kernel.sh`，50 个内核 C 对象和运行时编译链接成功，输出 `KERNEL BUILD OK`。
- 新生成的 `build/kernel.bin` 为 580498 字节，重新生成的 kernel_boot_test 验证完整启动日志通过。
- 文档自审检出 12 个任务、75 个待执行步骤、无占位词；此处仅记录基线，不表示新增功能已经实现。


## 最终执行状态（2026-10-17）

功能实现与完整模拟器回归已完成；游戏人工验收待执行。原任务步骤为计划快照，文件合并/接口调整及逐项证据见 [最终验收记录](../2026-10-17-interactive-shell-verification.md)。
