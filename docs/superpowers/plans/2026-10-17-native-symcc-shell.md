# Native symcc, Command Lists and 32MiB Platform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. User selected native execution in this session; preserve that method and edit the original repository.

**Goal:** 在游戏 shell 支持条件命令和 reset，并在扩容后的客户机中执行原生 symcc，编译 DiskA 上的 C 文件并直接运行。

**Architecture:** 分为三个可独立交付的子项目：shell 连接与清屏；32MiB 平台/文件系统；客户机 SDK 与原生编译链。复用现有 symcc 核心，先测量目标机器代码及内存，再决定阶段合并。编译、链接、SPX 生成和程序执行均发生在客户机。

**Tech Stack:** SymphonyPlus 32-bit big-endian ISA, Linux 0.11 port, MINIX v1, SPX1, C subset compiler, Python image tools, MSYS2 GCC and Git Bash.

**Spec:** [Approved design](../specs/2026-10-17-native-symcc-shell-design.md). 用户于 2026-10-17 确认执行该设计；本实施计划仍须审阅。

## Global Constraints

- 在当前项目原目录编辑，使用 Git 分支，不创建 worktree。
- 沿用 SymphonyPlus CPU/ISA、大端、DiskA MINIX 和 SPX 可执行格式。
- 默认候选配置为 32MiB RAM + 32MiB DiskA，2MiB 用户窗口，8 个进程槽；必要时可按设计升级到 64MiB RAM/4MiB 用户窗口，DiskA 仍为 32MiB。
- 继续保留历史 M3 独立构建；新系统不覆盖 build/m3/kernel.bin。
- shell 每行 256 字节缓冲、每命令最多 32 参数、整行最多 64 token、16 条简单命令、8 个 heredoc。
- heredoc 正文必须消费到结束标记；失败或短路不能使正文成为命令，编译/链接失败不能破坏旧输出。
- 客户机 SDK 的标准 puts 与原系统不自动换行的 puts 分开构建。
- 用户程序首版承诺 signed/unsigned 32 位，不以虚假的 int64_t 定义掩盖移植问题。
- 独立记录宿主构建、模拟器目标机器执行、游戏实际验证；不能把外部文件长度视为硬件容量证明。
- Reference/** 保持原换行，改动标记 SYMPLUS-PORT；使用支持 cr-at-eol 的 diff 检查。

## Review Focus

- 引号中的连接符、错误行末连接符：先解析整行，既不能误分词，也不能执行语法错误前的副作用（Task 1）。
- 短路 heredoc：必须读取正文但不能创建重定向目标，结束后 shell 仍能读下一行（Task 2）。
- 高位地址回绕与跨位图分配：分别观察实际地址读回和磁盘元数据，不能仅测 32MiB 数组长度（Tasks 4–6）。
- realloc/数值解析溢出：保留原对象、返回明确失败，不能覆盖栈或截断 0xffffffff（Tasks 7–9）。
- 编译失败后的旧二进制：旧内容保持且 && 不执行它；成功产物重启后仍能加载（Tasks 10–12）。

## Execution Conventions

所有命令在仓库根目录执行。PowerShell 先设置：

```powershell
$env:PATH='D:\Downloads\msys64\ucrt64\bin;'+$env:PATH
$env:PYTHONUTF8='1'
```

每任务执行 red → green → 相关回归，提交可独立审阅的更改。先确认失败源于缺失行为，不能以构建报错冒充功能回归失败。
所有实现步骤先完成该任务的测试，再实现列出的接口与规则。生成文件由其脚本重建，不手工维护两份常量。

## Subproject A: Shell command lists and reset

### Task 1: Parse complete command lists without side effects

**Files:** 新建 `user/include/shell.h`、`user/lib/shell_parse.c`、`tests/shell_parse_test.c`；修改 `user/bin/sh.c`、`scripts/build_user.sh`、`Makefile`。

**Interfaces:**

```c
#define SH_COMMANDS 16
enum { SH_ALWAYS, SH_AND, SH_OR };
typedef struct {
    char *argv[33]; int argc,when,append,here_fd;
    char *input,*output,*delimiter;
} ShCommand;
typedef struct { ShCommand command[SH_COMMANDS]; int count; } ShList;
int sh_parse(char *line,char *storage,int storage_size,ShList *out);
/* 0=valid, -1=syntax/capacity error. Does not perform I/O or execute. */
```

- [ ] 编写纯解析测试：`cd temp && ls`、`a||b;c;`、`echo 'a&&b'`、`echo a\\&b`、`echo x >f && cat <f`；断言 argv、when 和重定向归属，不在测试里复制解析器。
- [ ] 添加无副作用回归：`echo lost>/tmp/keep &&`、`a & b`、`a || || b`、超过 16 命令/64 token；在真实 shell 测试中断言 `/tmp/keep` 未改动。
- [ ] 运行 `mingw32-make tests/shell_parse_test.exe` 及对应 CLI 用例，观察现有实现拒绝合法连接或错误行被错误执行的失败。
- [ ] 实现 SH_AND/SH_OR/SH_ALWAYS token，重定向优先于连接符；支持引号/转义；禁止单 &、单 |；只允许尾部 `;`。整行语法和容量验证完成后才产生可执行 ShList。

```c
/* Parsing invariant at every list separator: */
if (!current->argc) return -1;
if (list->count == SH_COMMANDS) return -1;
/* && -> next.when=SH_AND; || -> SH_OR; ; -> SH_ALWAYS */
```

- [ ] 将 shell 专用解析对象加入 sh 的构建，其他 /bin 程序不重复链接；Task 1 只交付解析 API，Task 2 才切换 sh 的执行入口，避免半实现的连接符被静默忽略。运行解析及旧 `--ui/--files/--symbols` 测试，提交 `feat: parse bounded shell command lists`。

### Task 2: Execute statuses, consume heredocs, and clear with reset

**Files:** 修改 `user/bin/sh.c`、`tests/shell_test.c`、`tests/test_emu_cli.py`、`Makefile`。

**Consumes:** Task 1 的 ShList/ShCommand。**Produces:** 每条实际执行命令返回 0..255；shell 保存最后状态。

- [ ] 增加客户机 `--lists` 模式：失败 cd 后 && 不执行、|| 执行，`a && b || c` 左结合，`;` 总执行，未知命令和 `exit 7` 有非零状态；引号内符号输出为字面量。
- [ ] 增加 `--reset` 模式：cd /etc、输出标记、reset，观察首次清屏后的最后非空行是 `symplus:/etc$`，旧标记消失；再执行 pwd，目录仍为 /etc。不要用宿主清屏代替客户机输出。
- [ ] 增加短路 heredoc 回归：`nosuch && cat >/tmp/skipped <<E` 后输入 `rm /etc/keep`、`E`；断言 keep 未删、skipped 不存在且后续命令运行。增加第 9 个 heredoc 的容量拒绝用例。
- [ ] 执行上述新用例，观察 red；保留完整解析与此前文件测试，不以 echo 的命令回显作为执行结果。
- [ ] 在执行前按语法顺序收集全部 heredoc，发生错误则 drain 并关闭已打开的 spool；根据状态短路，只有执行分支才打开重定向。

```c
run = c->when==SH_ALWAYS || (c->when==SH_AND && last==0)
      || (c->when==SH_OR && last!=0);
/* A skipped command leaves last unchanged. */
/* external success: last=(wait_status>>8)&255; failed exec: 127 */
```

- [ ] 将 cd 的错误明确映射为非零；fork/wait/重定向错误也非零。实现 exit N 的数字与参数检查、省略值用最后状态。
- [ ] 实现 reset 无参数内建，输出 `\033[2J\033[H` 并返回 0；错误参数返回非零；沿用 stdout 重定向与恢复规则。运行新模式、CLI 和完整 `mingw32-make test`，提交 `feat: execute shell conditions and reset terminal`。

## Subproject B: Capacity and 32MiB platform

### Task 3: Configure disjoint RAM regions and matching ABI bridges

**Files:** 修改 `runtime/platform.h`、`scripts/gen_platform.py`、`runtime/platform.sym`、`Reference/Linux-0.11-main/kernel/symplus/process.c`、`Reference/Linux-0.11-main/fs/symexec.c`、`emu/emu_main.c`、`scripts/build_system.sh`、`tests/platform_test.c`、`tests/shell_test.c`、`tests/test_emu_cli.py`；新建 `scripts/platform_config.py`、`tests/test_platform_config.py`。

**Interfaces:**

```c
#define SP_RAM_BYTES     0x2000000u
#define SP_DISK_BYTES    0x2000000u
#define SP_USER_BASE     0x200000u
#define SP_USER_SIZE     0x200000u
#define SP_CONTEXT_ADDR  0x180000u
#define SP_KERNEL_STACK  0x200000u
#define SP_EXEC_STAGE    0x400000u
#define SP_ARG_STAGE     0x600000u
#define SP_SNAPSHOT_BASE 0x1000000u
#define SP_MAX_USERS     8u
```

Python 配置读取这些整数定义；校验所有区域范围及快照总大小，不能复制一份 Python 常量。

- [ ] 更新 platform 测试为 `snapshot_base + users*window == ram_bytes`，逐对检查 active/stage/args/snapshot 与上下文/内核栈保留区；生成桥的地址须与头文件一致。
- [ ] 运行 platform 测试观察旧 8MiB 布局失败，然后实现统一常量和桥生成；syscall 仍保存完整现场，BSS/内核栈/参数区绝不落入用户快照。
- [ ] exec 使用 stage/arg 常量和新窗口，在所有 fallible 操作结束后才提交；扩容不改变 8KiB 参数限额和 SPX 版本/未知 flags 检查。
- [ ] CLI 默认 RAM 取配置，允许显式更大受检尺寸；拒绝数值溢出。修改旧系统测试的 RAM 分配与坏指针边界，保持 M3 单独尺寸；构建系统镜像补零到选定 RAM 大小。
- [ ] 重建并运行 `platform_test`、`shell_test --process`、`--stress`、execprobe/指针回归；仍须八槽耗尽与恢复，不降低进程槽绕过测试。提交 `feat: configure 32 MiB platform memory`。

### Task 4: Build non-destructive high-address capacity probes

**Files:** 新建 `runtime/capacity_probe.c`、`runtime/capacity_crt0.sym`、`scripts/build_capacity_probe.sh`、`tests/capacity_probe_test.c`；修改 `Makefile`、`emu/emu.c`、`emu/emu.h`。

**Interfaces:** 构建产物 `build/capacity-probe.bin`、`build/capacity-disk.bin`；独立裸机探针，不加载 Linux/文件系统，不操作用户 rootfs。共用校验函数接受 `unsigned (*read_word)(unsigned address)` / `void (*write_word)(unsigned address,unsigned value)` 回调，分别传入 RAM 与 DiskA 实现。
模拟器 EmuOptions 增加 `uint32_t ram_address_mask,disk_address_mask`；0 表示原始寻址，非零时设备访问先取 mask 再检查物理范围，用于模拟少接高地址线。

- [ ] 使用模拟器真实 load/store/pload/pstore 测试位置 `0x1000`、`0x801000`、`0x1001000`、`0x1fffffc`；先保存原值，写不同模式并断言分别读回。
- [ ] 用 EmuOptions 的 `0x7fffff` mask 在 32MiB 测试存储上模拟 8MiB 地址回绕，确保探针报告 ALIAS 而非 PASS；容量模型只用于模拟器硬件测试，不加入生产 shell。
- [ ] 执行测试观察没有探针的 red；实现先保存、写入、交叉读回、按逆序还原的算法，输出独立 `RAM 32M PASS` / `DISK 32M PASS` 或失败位置。

```c
/* Save all old values before changing any location. */
for (i=0;i<count;i++) old[i]=read_word(address[i]);
for (i=0;i<count;i++) write_word(address[i],pattern[i]);
for (i=0;i<count;i++) if(read_word(address[i])!=pattern[i]) ok=0;
for (i=count-1;i>=0;i--) write_word(address[i],old[i]);
```

- [ ] 探针复用内核镜像从 0x3000 开始的链接布局，0x1000 为保留 scratch，不能让其覆盖探针代码/栈/帧缓冲。验证正常/别名分支均还原存储；构建独立 32MiB 空白测试磁盘。产物说明只要求用户分别加载探针 RAM 和测试盘，不覆盖实际系统盘。
- [ ] 运行 `tests/capacity_probe_test.exe`，记录目标指令和游戏人工复验位置，提交 `test: add RAM and DiskA capacity probe`。

### Task 5: Generalize MINIX geometry and encode 32MiB rootfs

**Files:** 修改 `scripts/mkrootfs.py`、`Reference/Linux-0.11-main/kernel/blk_drv/symdisk.c`、`Reference/Linux-0.11-main/fs/bitmap.c`、`Reference/Linux-0.11-main/fs/super.c`、`tests/test_mkrootfs.py`、`tests/test_emu_cli.py`、`scripts/build_system.sh`。

**Interfaces:** `make_image(files,size_bytes=8*1024*1024)` 继续允许旧 fixture；CLI 构建系统显式传入 `SP_DISK_BYTES`。新增 `image_geometry(image)` 返回超级块几何和 inode_base/first_zone/nzones，独立读取器和测试用它定位。

- [ ] Python 先测试 8MiB/32MiB 镜像：32MiB 的 nzones=32768、zmap_blocks=4、inode_base=7168、first_zone=11；超过 v1 可表示容量必须报错。
- [ ] 增加真实内核分配回归：预占低 zone 后分别在第 2、3、4 位图块首次可用位置创建文件，保存并检查对应位从 0→1，rm 后从 1→0。
- [ ] 执行新测试观察 red，修改镜像布局与分配器：

```c
bit=zone-s->s_firstdatazone+1;
map=bit>>13; local=bit&8191;
if(map>=s->s_zmap_blocks || !s->s_zmap[map]) return 0;
/* s_zmap[map]->b_data[local>>3], mask=1<<(local&7) */
```

- [ ] sysdisk 请求范围使用 SP_DISK_BYTES/512 并以减法检查溢出；read_super 校验 bitmap/inode/data 几何与 v1 字段上限。旧 8MiB 超级块仍可挂载。
- [ ] 用超级块几何替换现有测试中固定的 inode 偏移/zone 8；保持独立坏镜像测试意图，不能通过删除断言迁移布局。
- [ ] 运行镜像/CLI/间接块损坏测试及完整 suite，提交 `feat: support 32 MiB MINIX root filesystem`。

### Task 6: Verify high-zone persistence and release

**Files:** 新建 `tests/disk_capacity_test.c`、`tests/test_disk_capacity.py`；修改 `Makefile`、`scripts/build_system.sh`。

**Consumes:** Task 5 合法 32MiB rootfs 和 Task 3 RAM 布局。**Produces:** 高 zone 读写/删除/重启的目标机器证据。

- [ ] 构造有明确分配位图与文件占用的 fixture，让客户机写入的下一块位于 8MiB、16MiB、24MiB 之后；不能伪造位图却把 live 文件声明为空。
- [ ] 注入客户机 heredoc/文件操作，使用 `sync` 后保存镜像，独立检查内容和位图；重启执行 cat 读取唯一内容，然后 rm 并验证释放。
- [ ] 观察旧固定 8MiB 驱动的失败，再运行扩容实现；检查超级块、其他文件与保留元数据位未改变。
- [ ] 把测试加入完整 suite；预算依据目标指令数而不是固定 sleep，结束条件为真实程序输出。
- [ ] 提交 `test: verify high-zone disk persistence and deletion`。

## Subproject C: Native runtime and symcc

### Task 7A: Add checked per-process heap allocation

**Files:** 修改 `Reference/Linux-0.11-main/kernel/symplus/process.c`、`Reference/Linux-0.11-main/fs/symexec.c`、`Reference/Linux-0.11-main/include/linux/symplus.h`、`user/include/sp.h`、`user/lib/libc.c`；新建 `user/native/heap.c`、`tests/native_heap_test.c`、`tests/test_native_runtime.py`。

**Interfaces:**

```c
unsigned sp_brk(unsigned requested); /* nr45; 0 queries current break */
void *malloc(unsigned size); void free(void *p);
void *calloc(unsigned n,unsigned size); void *realloc(void *p,unsigned size);
```

平台维护每进程 heap_base/current/limit，exec 设置为对齐的镜像 BSS 末尾与保留栈底之间，fork 继承；非法 brk 返回当前值，SDK 比较请求判断失败。

- [ ] 测试分配/释放/合并/对齐、realloc 失败保留旧对象、calloc 乘法溢出；实际客户机写堆并检查内核与栈哨兵，不仅测试宿主 allocator。
- [ ] 运行这些用例观察 red；实现受检 break 与可复用 free-list/coalesce 分配器，禁止对 mmap/硬件区域假定无限空间。

```c
if(size && n>~0u/size) return 0; /* calloc */
/* realloc allocates/copies before freeing old when in-place growth fails. */
```

- [ ] 运行运行库、fork、exec、坏指针回归，提交 `feat: add checked native heap allocation`。

### Task 7B: Add executable permissions and atomic publication

**Files:** 修改 `Reference/Linux-0.11-main/kernel/symplus/process.c`、`Reference/Linux-0.11-main/fs/namei.c`、`user/include/sp.h`、`user/lib/libc.c`、`tests/test_native_runtime.py`。

**Interfaces:** `int chmod(char *path,int mode)` 使用 nr15；`int rename(char *old,char *new)` 使用 nr38，只支持同一文件系统的普通文件原子替换。

- [ ] 测试 chmod 使完整 SPX 可执行；rename 覆盖普通目标、源缺失、目标为目录、跨设备和同一名字，失败时旧目标字节不变。
- [ ] 运行客户机 probe，观察 syscall ENOSYS 或旧文件被破坏的 red；分开检查返回码与独立镜像读取的实际文件内容。
- [ ] 暴露既有 sys_chmod；在 namei.c 现有查找/目录修改助手旁实现普通文件 sys_rename。对两个用户字符串分别做范围检查，限制 regular file/same device，保留 zone 校验。

```c
/* Resolve/validate both parents, source and target; allocate missing target
 * entry before committing. No fallible operation may remain here. */
target_entry->inode=source_inode->i_num;
source_entry->inode=0;
/* Mark both directory buffers dirty; release old target inode afterwards. */
```

- [ ] 提交前不 unlink 旧输出；同一 inode/名字不意外降低链接数；目录目标必须失败。断电恢复不在本次原子命名空间操作的承诺内。
- [ ] 运行 syscall/文件/损坏间接块回归，提交 `feat: publish executable files atomically`。

### Task 8: Build compiler-compatible FILE, formatting and SDK headers

**Files:** 新建 `user/native/include/{stdio,stdlib,string,ctype,stddef,stdarg,stdint,stdbool,limits}.h`、`user/native/{stdio,format,string,convert}.c`、`scripts/build_native_runtime.sh`、`tests/native_libc_probe.c`；修改 `scripts/build_user.sh`、`scripts/mkrootfs.py`、`tests/test_native_runtime.py`。

**Interfaces:** FILE 包含 fd、读/写模式、错误/EOF 状态和可选小缓冲；导出编译器实际引用的 fopen/fclose/fread/fwrite/fseek/ftell/fgets/fputs/fputc/fflush、printf/fprintf/snprintf/vsnprintf、字符串/ctype/整数转换。
SDK 为 `/include/*.h`，启动与运行库对象位于 `/lib`；所有文件名不超过 14 字节。

- [ ] 逐个列出 symcc/asm 核心的外部 libc 引用，与 SDK 实现符号表对比；缺少的接口必须实现或显式移除调用，不能空函数返回成功。
- [ ] 客户机 probe 验证 FILE 往返/定位/EOF、短读写/失败、字符串、`%.*s`、整数极值和截断 snprintf 返回值；`puts("line")` 应只增加一个换行。
- [ ] 观察缺失库的 red；实现可重用的受检读取/写入循环，标准流 fd0/1/2 不由普通 fclose 错误关闭，va_list 使用已有目标 ABI。

```c
/* snprintf counts all would-be output but terminates when cap>0. */
if(cap) destination[count<cap?count:cap-1]=0;
```

- [ ] SDK 链接独立构建的基础 syscall 对象，把原 raw puts 重命名为 sp_raw_puts；原 /bin/sh 等继续链接原 libc，不更改其输出语义。用户标准 puts 由 SDK 提供。
- [ ] 运行 SDK probe、旧 shell UI/文件测试，提交 `feat: add native compiler SDK runtime`。

### Task 9: Port shared compiler internals with explicit integer semantics

**Files:** 新建 `runtime/compiler_int.h`、`tests/native_core_probe.c`；修改 `symcc/src/{symcc.h,tokenize.c,parse.c,preprocess.c,codegen.c,obj.h,obj.c}`、`emu/asm.c`、`tests/{user_link_test.c,run_tests.c}`、`scripts/build_native_runtime.sh`。

**Interfaces:** `CcInt`/`CcUInt` 为内部值/位模式抽象；宿主保留真实 64 位中间值，SP_NATIVE 路径按明确的 32 位目标语义处理。数值解析返回成功/范围错误与 end 指针，绝不默默截断源文字面量。

- [ ] 加入 literal 0xffffffff、2147483647、-2147483648、过范围十进制/十六进制、尺寸乘法溢出、unsigned 比较/右移和格式化 ABI 回归；host 与 target 各自执行，允许明确诊断不支持的 64 位操作。
- [ ] 观察 native 缺失/错值的 red；引入共享内部整数抽象并逐个审计位移、解析、输出格式与数组尺寸。SDK 不定义假的 int64_t；客户机汇编器拒绝真正 U64 编码需求，宿主保持原支持。
- [ ] 将 Obj 行缓存改为几何增长，保留外部 .sym 序列化格式；realloc 失败必须保留旧指针并退出非零。

```c
/* Example target literal accumulation check before multiply/add. */
if(value>(0xffffffffu-digit)/base) return CC_RANGE;
value=value*base+digit;
```

- [ ] 使用宿主 symcc + SDK 实际生成预处理、parse/codegen 和链接核心目标对象，记录每阶段 text/data/BSS/栈保留/峰值堆；在 `build/native/footprint.json` 输出度量和是否超出预算。
- [ ] 若 2MiB 内不满足，先移除整块预留/无界缓存并分阶段；仍不满足时按设计切换 64MiB 配置，重跑 Tasks 3–6，保持 8 进程槽。不得删减 C parser 为只匹配示例。
- [ ] 运行全部宿主编译器回归和 target probe，提交 `refactor: make compiler core portable to native 32-bit runtime`。

### Task 10: Link and run native compiler stages

**Files:** 新建 `user/native/{cpp_main,cc1_main,ld_main}.c`、`scripts/build_native_compiler.sh`、`tests/test_native_compiler.py`；修改 `symcc/src/link_image.c`、`scripts/build_system.sh`、`scripts/mkrootfs.py`、`Makefile`。

**Interfaces:**

- `/bin/scpp input.c output.i [-I dir]` 使用 preprocess_text；SDK 默认 include 搜索 `/include`。
- `/bin/scc1 input.i output.sym` 调用 tokenize → parse → codegen(obj,true)，写 .sym。
- `/bin/sld input.sym output.spx` 用 SDK crt0、syscall、libc、整数对象，调用用户镜像链接/汇编编码，写当前 SPX 地址布局。

- [ ] 先测试 cpp 的 include/macro、cc1 的函数/循环和 ld 的未定义符号/可执行头，不通过 host 生成用户测试程序来冒充 native 阶段。
- [ ] 观察阶段命令缺失的 red；写独立驱动，分别链接必要核心，失败诊断到 stderr、返回非零。

```c
/* cc1 consumes preprocessed source, so it does not relink preprocess.c. */
tokens=tokenize(source); program=parse(tokens);
if(!codegen(program,obj,1)) return 1;
/* ld uses SP_IMAGE_USER; .spx contains SP_EXEC_MAGIC and current address limit. */
```

- [ ] native linker 的 legacy 分支明确拒绝或条件编译，不为了未用的宿主链接路径引入全套旧链接器；保留宿主 symld_link 接口与测试。
- [ ] 链接缓冲和编码缓冲按真实载入长度/目标上界分配，不能用“全部汇编文本长度加常数”的未检查大分配；记录峰值，检查生成代码与 SDK 是否适配当前窗口。
- [ ] 运行真实客户机阶段用例并加载生成 SPX，提交 `feat: run native preprocessing compilation and linking stages`。

### Task 11: Expose /bin/symcc with safe output handling

**Files:** 新建 `user/bin/symcc.c`；修改 `scripts/build_user.sh`、`scripts/build_native_compiler.sh`、`tests/test_native_compiler.py`。

**Consumes:** Task 10 阶段命令、Task 7B rename/chmod、Task 8 SDK。**Produces:** `symcc file.c -o main` 和默认 a.out 的用户接口。

- [ ] 写端到端：heredoc 在运行中创建 main.c，原生 symcc 编译，`&& ./main` 打印唯一标记；第二个不同源码在同一 shell 编译并运行。
- [ ] 加入错误测试：语法错/缺头文件/未定义符号/非法选项/长文件名/输出父目录不存在；原旧可执行字节保持，&& 后右侧标记不出现。
- [ ] 观察命令缺失的 red；实现单源文件、-o、-I 和可用阶段选项，未知选项返回非零。所有临时名 PID+序号且 basename<=14，O_EXCL 创建，逐次 fork/exec/wait 保留编译器诊断。

```c
if(run_cpp()!=0 || run_cc1()!=0 || run_link()!=0) goto failed;
if(chmod(temporary_output,0755)<0) goto failed;
if(rename(temporary_output,output)<0) goto failed;
/* failed: close/remove temporaries; never exec or overwrite old output. */
```

- [ ] 编译错误也清理阶段中间文件和描述符；最终发布只在完整 SPX 已写入并关闭后进行，检查 rename 返回值。成功时 exit0、失败非零，与 &&/|| 连通。
- [ ] 客户机 sync 保存磁盘，重新启动运行生成程序；重复编译检查目录条目及进程槽没有泄漏。提交 `feat: compile and execute C programs with native symcc`。

### Task 12: Full validation, branch review and delivery

**Files:** 修改 `Makefile`、`README.md`、`AGENTS.md`、`emu/emu_main.c`、`tests/test_emu_cli.py`；新建 `docs/superpowers/2026-10-17-native-symcc-verification.md`、`scripts/package_system.py`。

- [ ] 把解析、容量、SDK 和 native compiler 测试纳入完整命令，运行 `mingw32-make test`。原有模式不得删除；如需增加 compiler 指令预算，先输出观测到的目标指令数并保持实际完成断言。
- [ ] CLI 增加 `--max-instructions N`，检查无符号整数范围、拒绝 0/负数/尾随垃圾。用故意不产生结束标记的目标程序验证耗尽预算时报告未完成并返回非零；不能以 exit0 或缺失标记宣称成功。interactive 持续等待保留。验证新配置坏尺寸、截断文件、磁盘元数据损坏仍失败明确。
- [ ] 按 requesting-code-review 请求一次独立整分支审阅，仅传设计/计划/基线/最终 diff。修复 actionable 问题并重跑受影响和完整测试。
- [ ] 记录所有新镜像大小/SHA-256、native 阶段峰值内存、编译/运行指令数、容量探针结果；游戏未操作时明确标记待游戏验证。
- [ ] 打包 `kernel.bin`、`rootfs.bin`、独立容量探针/测试盘及加载说明，核验 zip 解包内容与已测试镜像逐字节一致。README 显示 `reset`、连接符、编译命令和 32MiB 加载位置。
- [ ] 提交 `docs: deliver verified native symcc system and capacity probes`，确认工作区状态；给用户两条游戏验收路径：先探针确认寻址，再加载系统进行截图流程与编译运行。

## Plan Self-Review

- shell 词法/状态/reset/heredoc → Tasks 1–2；RAM/桥/exec/容量探针 → Tasks 3–4；磁盘几何/高位图持久化 → Tasks 5–6。
- 堆 → Task 7A；权限/失败输出保护 → Task 7B；标准运行库和 SDK → Task 8；整数语义/原核心移植/内存度量 → Task 9。
- 客户机各阶段与全链路用户接口 → Tasks 10–11；整体回归、独立审阅和游戏证据边界 → Task 12。
- 五项 Review Focus 均有归属测试；未以新增磁盘大小、宿主编译或预生成用户程序代替原生编译验收。
- 执行方式沿用用户已选的本会话直接实现；计划审阅通过后使用 executing-plans，不再要求选择执行方式。
