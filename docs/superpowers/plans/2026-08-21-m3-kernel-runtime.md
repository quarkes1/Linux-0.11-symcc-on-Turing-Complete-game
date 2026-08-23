# M3 内核运行期验证（最小启动）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 Linux 0.11 内核在模拟器与《Turing Complete》游戏内真实跑通启动链：11 行进度日志经 console 重定向输出到游戏屏，fork 前停机。

**Architecture:** 不改 ISA 与游戏电路，纯 C 改写内核两处——console.c（con_init 硬编码 96×40 几何、con_write/scrup 等改字节单元写游戏帧缓冲）与 init/main.c（各 init 调用后插 printk 日志、fork 块替换为停机点）；构建管线追加 asm.exe 产出 kernel.bin；新增 emu 端到端测试断言帧缓冲内容。

**Tech Stack:** C89（symcc 交叉编译）、symld 链接器、emu/asm.exe 汇编器、emu/emu.exe 模拟器、Makefile（mingw32-make）、Linux 0.11 内核源码。

**Spec:** `docs/superpowers/specs/2026-08-21-m3-kernel-runtime-design.md`（已审阅通过，2026-08-21）

## Global Constraints

- 所有内核改动带 `/* SYMPLUS-PORT: M3 ... */` 注释（git 可追溯）。
- 不改动 SymphonyPlus ISA 与游戏内 CPU 电路；不引入 tick/调度/syscall（M4 范围）。
- 沙盒红线：绝不向游戏粘贴 kernel.asm（3.4MB 文本让游戏重汇编卡死）；只走 RAM"外部文件"模式加载 kernel.bin。
- 屏幕几何硬编码：帧缓冲 0x2000、96 列、40 行（与 `symcc/include/config.h` 的 FRAMEBUF_BASE/COLS/ROWS 一致）；**字节单元**（每字符 1 字节，属性字节丢弃，颜色由 crt0 白字黑底提供）。
- 日志文本逐字（11 行）：10 行 init 日志（`mem_init OK` … `floppy_init OK (stub)`）+ `"\nBoot complete. Halting.\n"`。
- 构建/测试命令：`bash scripts/build_kernel.sh`；`export PATH="/d/Downloads/msys64/mingw64/bin:$PATH" && mingw32-make test`（DLL 遮蔽已知问题）。
- README 无 emoji；提交信息无 Co-Authored-By 行。
- 已核实事实（2026-08-23 实证，勿重复调查）：
  - printk → tty_write(0,buf,i)（`kernel/printk.c:29` 已有 SYMPLUS-PORT 直调）；tty_write 的 O_POST/ONLCR 把 `\n` 转成 `\r\n`（`kernel/chr_drv/tty_io.c:304-316`）——con_write 无需改换行逻辑，屏幕自然左对齐逐行。
  - outb_p/inb_p 已是空操作桩（`include/asm/io.h:8-9`）——set_cursor/sysbeep 等端口操作自动空操作，无需改动。
  - 镜像布局（`build/kernel.asm` 实测）：bss 0xC008-0xEE08、代码/数据高区 0x85xxx 起、栈顶 sp=0x4000 向下（帧缓冲 0x2000-0x2F00 之下，余量 4352 字节）——**与帧缓冲无重叠**。
  - emu_run 主存 calloc 清零（`emu/emu.c:52`），镜像外 RAM=0 与游戏一致。
  - **停机机制（计划关键偏差，见 Task 2）**：不能用 `for(;;)` —— symld 把用户代码的符号跳转全部桩化，空循环自跳转变成 `load_32 r13,[池槽]; jmp r13` 且池槽=桩首地址，形成 load→jmp 乒乓，emu 的"目标==pc"停机判定（emu.c:125/131）永不触发（2026-08-23 实测 max_instr 超限）。改用 `main()` 返回 → crt0 尾部链接器统一输出的 `halt: jmp halt`（直接编码自跳）——与全部既有 run_tests 测试同一停机机制；游戏内表现一致：停在自循环、不冻结。
  - 游戏 RAM/磁盘大小用户可随时调整，调试时均设 8MB（emu_run `8<<20` 一致；M3 不访问磁盘）。

---

### Task 1: console.c 重定向（字节单元 + 硬编码几何）

**Files:**
- Modify: `Reference/Linux-0.11-main/kernel/chr_drv/console.c`（gotoxy 88-95、scrup 非 EGA 分支 154-167、scrdown 非 EGA 分支 186-199、cr 222-226、del 228-235、csi_J 237-265、csi_K 267-297、insert_char 336-348、delete_char 363-376、con_write 445-603、con_init 615-686）

**Interfaces:**
- Consumes: 无（纯内部改写）
- Produces: `void con_init(void)` 硬编码 96×40/0x2000 几何；`void con_write(struct tty_struct *)` 以字节单元写帧缓冲——供 Task 4 测试断言屏幕内容

- [ ] **Step 1: con_init 硬编码几何（替换整个函数体）**

```c
void con_init(void)
{
	register unsigned char a;

	/* SYMPLUS-PORT: M3 不读 0x90000 BIOS 参数区（游戏 RAM 无此数据），
	 * 硬编码游戏屏几何：ASCII 8 文本 96×40、帧缓冲 0x2000
	 * （与 symcc/include/config.h 的 FRAMEBUF_BASE/COLS/ROWS 一致） */
	video_num_columns = 96;
	video_size_row = video_num_columns;	/* 字节单元：每字符 1 字节（游戏屏纯 ASCII） */
	video_num_lines = 40;
	video_page = 0;
	video_erase_char = 0x20;	/* ' ' */
	video_mem_start = 0x2000;
	video_mem_end = video_mem_start + video_num_lines * video_size_row;
	video_port_reg = 0;		/* 无端口（outb_p/inb_p 为桩） */
	video_port_val = 0;
	video_type = VIDEO_TYPE_CGA;	/* 走非 EGA 简单滚屏路径（无 EGA 快速滚动） */

	/* Initialize the variables used for scrolling	*/

	origin	= video_mem_start;
	scr_end	= video_mem_start + video_num_lines * video_size_row;
	top	= 0;
	bottom	= video_num_lines;

	/* SYMPLUS-PORT: M3 起始光标 (0,0)（不读 ORIG_X/ORIG_Y） */
	gotoxy(0,0);
	set_trap_gate(0x21,&keyboard_interrupt);
	outb_p(inb_p(0x21)&0xfd,0x21);
	a=inb_p(0x61);
	outb_p(a|0x80,0x61);
	outb(a,0x61);
}
```

说明：原函数的 ORIG_* 读取、video_type 探测、display_desc 字符串写屏（"EGAc" 4 字符写进视频内存）整块删除——display_desc 在字节单元下会散落在第 0 行，必须删。

- [ ] **Step 2: gotoxy / cr / del 光标数学字节化**

`gotoxy`（原 94 行）：

```c
	/* SYMPLUS-PORT: M3 字节单元（游戏屏每字符 1 字节），不再 x<<1 */
	pos=origin + y*video_size_row + x;
```

`cr`（原 224 行）：

```c
static void cr(void)
{
	/* SYMPLUS-PORT: M3 字节单元 */
	pos -= x;
	x=0;
}
```

`del`（原 230-234 行）：

```c
static void del(void)
{
	if (x) {
		pos -= 1;	/* SYMPLUS-PORT: M3 字节单元 */
		x--;
		*(unsigned char *)pos = video_erase_char;
	}
}
```

- [ ] **Step 3: csi_J / csi_K / insert_char / delete_char 字节化**

`csi_J`（原 242-264 行）——count 去掉 `>>1`，循环改字节：

```c
	switch (par) {
		case 0:	/* erase from cursor to end of display */
			count = scr_end-pos;	/* SYMPLUS-PORT: M3 字节单元（原 >>1） */
			start = pos;
			break;
		case 1:	/* erase from start to cursor */
			count = pos-origin;	/* SYMPLUS-PORT: M3 字节单元 */
			start = origin;
			break;
		case 2: /* erase whole display */
			count = video_num_columns * video_num_lines;
			start = origin;
			break;
		default:
			return;
	}
/* SYMPLUS-PORT: rep stosw erase -> C loop（M3 字节单元） */
	{
		unsigned char *_p = (unsigned char *)start;
		long _i;
		for (_i = 0; _i < count; _i++)
			_p[_i] = video_erase_char;
	}
```

`csi_K`（原 270-296 行）——两处 `start = pos - (x<<1)` 改 `start = pos - x`，循环同 csi_J 改字节：

```c
	switch (par) {
		case 0:	/* erase from cursor to end of line */
			if (x>=video_num_columns)
				return;
			count = video_num_columns-x;
			start = pos;
			break;
		case 1:	/* erase from start of line to cursor */
			start = pos - x;	/* SYMPLUS-PORT: M3 字节单元 */
			count = (x<video_num_columns)?x:video_num_columns;
			break;
		case 2: /* erase whole line */
			start = pos - x;	/* SYMPLUS-PORT: M3 字节单元 */
			count = video_num_columns;
			break;
		default:
			return;
	}
/* SYMPLUS-PORT: rep stosw erase -> C loop（M3 字节单元） */
	{
		unsigned char *_p = (unsigned char *)start;
		long _i;
		for (_i = 0; _i < count; _i++)
			_p[_i] = video_erase_char;
	}
```

`insert_char`（原 336-348 行）：

```c
static void insert_char(void)
{
	int i=x;
	unsigned char tmp, old = video_erase_char;
	unsigned char * p = (unsigned char *) pos;

	while (i++<video_num_columns) {
		tmp=*p;
		*p=old;
		old=tmp;
		p++;
	}
}
```

`delete_char`（原 363-376 行）：

```c
static void delete_char(void)
{
	int i;
	unsigned char * p = (unsigned char *) pos;

	if (x>=video_num_columns)
		return;
	i = x;
	while (++i < video_num_columns) {
		*p = *(p+1);
		p++;
	}
	*p = video_erase_char;
}
```

- [ ] **Step 4: scrup / scrdown 非 EGA 分支字节化**

`scrup` 的 `else /* Not EGA/VGA */` 分支（原 154-167 行）——EGA 快速滚动分支（109-152 行）保持不动：video_type 已硬编码 VIDEO_TYPE_CGA（Task 1 Step 1），EGA 分支是死代码：

```c
	else		/* Not EGA/VGA */
	{
/* SYMPLUS-PORT: rep movsl+stosw scroll -> C loop（M3 字节单元，游戏屏）
 * video_type 硬编码 CGA，EGA 快速滚动分支为死代码，未改写 */
		{
			unsigned char *_d = (unsigned char *)(origin + video_size_row*top);
			unsigned char *_s = (unsigned char *)(origin + video_size_row*(top+1));
			unsigned char *_e = (unsigned char *)(origin + video_size_row*(bottom-1));
			int _i;
			for (_i = 0; _i < (bottom-top-1)*video_num_columns; _i++)
				_d[_i] = _s[_i];
			for (_i = 0; _i < video_num_columns; _i++)
				_e[_i] = video_erase_char;
		}
	}
```

`scrdown` 的 else 分支（原 186-199 行）同样处理：

```c
	else		/* Not EGA/VGA */
	{
/* SYMPLUS-PORT: std rep movsl+stosw scroll-down -> C reverse loop（M3 字节单元） */
		{
			unsigned char *_d = (unsigned char *)(origin + video_size_row*(top+1));
			unsigned char *_s = (unsigned char *)(origin + video_size_row*top);
			unsigned char *_e = (unsigned char *)origin;
			int _i;
			for (_i = (bottom-top-1)*video_num_columns - 1; _i >= 0; _i--)
				_d[_i] = _s[_i];
			for (_i = 0; _i < video_num_columns; _i++)
				_e[_i] = video_erase_char;
		}
	}
```

- [ ] **Step 5: con_write 三处（字符写 / 退格 / tab）**

普通字符（原 461-464 行）：

```c
				/* SYMPLUS-PORT: M3 attr+char 双字节写 -> 单字符字节写
				 * （游戏屏纯 ASCII，颜色由 crt0 白字黑底设置提供） */
					*(unsigned char *)pos = (unsigned char)c;
					pos += 1;
					x++;
```

退格（原 473-477 行）：

```c
				else if (c==8) {
					if (x) {
						x--;
						pos -= 1;	/* SYMPLUS-PORT: M3 字节单元 */
					}
				}
```

tab（原 478-487 行）：

```c
				} else if (c==9) {
					c=8-(x&7);
					x += c;
					pos += c;	/* SYMPLUS-PORT: M3 字节单元 */
					if (x>video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					c=9;
```

- [ ] **Step 6: 构建回归（编译门）+ 提交**

Run: `export PATH="/d/Downloads/msys64/mingw64/bin:$PATH" && bash scripts/build_kernel.sh`
Expected: 50 对象编译 + 链接成功，输出 `KERNEL BUILD OK`（console.c 改动不破坏编译；行为验证在 Task 4）

```bash
git add Reference/Linux-0.11-main/kernel/chr_drv/console.c
git commit -m "feat(M3): console 重定向到游戏帧缓冲（96×40 字节单元 + 硬编码几何）"
```

---

### Task 2: init/main.c 进度日志 + 停机点

**Files:**
- Modify: `Reference/Linux-0.11-main/init/main.c`（extern 区 42-53、fork/pause inline 声明 23-24、启动链 124-146）

**Interfaces:**
- Consumes: `printk`（`include/linux/kernel.h:7` 有 `int printk(const char *fmt, ...);` 声明；本文件加 extern）
- Produces: 屏幕输出 11 行日志（Task 4 断言内容）；`main()` 以 `return` 结束 → crt0 尾部 `halt: jmp halt` 停机

- [ ] **Step 1: 加 printk 声明**

在 44 行 `extern int vsprintf();` 之后加一行：

```c
extern int printk(const char *fmt, ...);
```

- [ ] **Step 2: 删除未使用的 fork/pause inline 声明**

删除 23-24 行两行（`for(;;) pause();` 与 fork 块被替换后它们不再被引用；symcc 对未使用 static inline 的行为未验证，删掉保证确定性）：

```c
/* SYMPLUS-PORT: M3 删去 fork/pause 的 inline _syscall 声明——main() 不再
 * fork/pause（停机点见下），M4/M5 引入 fork/调度时再恢复 */
```

- [ ] **Step 3: 启动链插 10 行日志**

把 124-133 行（`mem_init(...)` 到 `floppy_init();`）替换为：

```c
	mem_init(main_memory_start,memory_end);
	printk("mem_init OK\n");
	trap_init();
	printk("trap_init OK (stub)\n");
	blk_dev_init();
	printk("blk_dev_init OK\n");
	chr_dev_init();
	printk("chr_dev_init OK\n");
	tty_init();
	printk("tty_init OK\n");
	time_init();
	printk("time_init OK (stub)\n");
	sched_init();
	printk("sched_init OK (stub)\n");
	buffer_init(buffer_memory_end);
	printk("buffer_init OK\n");
	hd_init();
	printk("hd_init OK (stub)\n");
	floppy_init();
	printk("floppy_init OK (stub)\n");
```

- [ ] **Step 4: 替换 fork 块为停机点**

把 134-146 行（`sti();` 之后到 `for(;;) pause();`）替换为：

```c
	sti();
	move_to_user_mode();		/* 空 stub */
	/* SYMPLUS-PORT: M3 最小启动——fork/调度属 M4/M5，此处停机。
	 * 不用 for(;;)：symld 把用户代码的自跳转桩化为 load->jmp 乒乓循环，
	 * 模拟器按"目标==pc"判定停机将永不触发（2026-08-23 实测超限）。
	 * main() 返回后由 crt0 尾部链接器统一输出的 "halt: jmp halt" 自跳
	 * 停机——与全部既有端到端测试同一机制；游戏内表现为空转不冻结。 */
	printk("\nBoot complete. Halting.\n");
	return;
```

- [ ] **Step 5: 构建 + 文本断言**

Run: `export PATH="/d/Downloads/msys64/mingw64/bin:$PATH" && bash scripts/build_kernel.sh`
Expected: `KERNEL BUILD OK`

Run: `grep -c '"Boot complete. Halting."' build/kernel.asm` — 字符串字面量以 asm 数据形式存在
Expected: `1`（其余 10 行日志串同理各 1 处，抽查 `grep -c '"tty_init OK"' build/kernel.asm` = 1）

- [ ] **Step 6: 提交**

```bash
git add Reference/Linux-0.11-main/init/main.c
git commit -m "feat(M3): 启动链逐阶段日志 + main 返回停机（crt0 尾部 jmp halt）"
```

---

### Task 3: build_kernel.sh 产出 kernel.bin

**Files:**
- Modify: `scripts/build_kernel.sh`（末尾）

**Interfaces:**
- Consumes: `build/kernel.asm`（symld 产物）
- Produces: `build/kernel.bin`（大端机器码，游戏外部文件模式与 Task 4 测试共用）

- [ ] **Step 1: 追加汇编步骤**

把 45 行 `echo "KERNEL BUILD OK"` 前插入：

```bash
# 汇编为游戏可直接加载的机器码（RAM"外部文件"模式原样装载、不做汇编）
../../emu/asm.exe ../../build/kernel.asm ../../build/kernel.bin
echo "== assembled kernel.bin: $(wc -c < ../../build/kernel.bin) bytes"
echo "KERNEL BUILD OK"
```

注意：脚本 19 行 `cd "$K"`（Reference/Linux-0.11-main 内），相对路径 `../../emu/asm.exe` 与编译段的 `"$CC"`/`"$LD"` 一致。

- [ ] **Step 2: 全量构建**

Run: `export PATH="/d/Downloads/msys64/mingw64/bin:$PATH" && bash scripts/build_kernel.sh`
Expected: `KERNEL BUILD OK`，`build/kernel.bin` 存在（约 570KB，与既有 581903 字节同量级；精确值因日志字符串数据而略有增长）

- [ ] **Step 3: 模拟器冒烟运行**

Run: `./emu/emu.exe build/kernel.bin`
Expected: 进程正常结束并打印 `exit_code = <某值>`（r1 残值，值不重要）——**不得**打印 `error: instruction limit exceeded`。若超限：启动链某处在打转（先查 Task 2 Step 3/4 是否按计划落地，再跑 Task 4 测试定位）。

- [ ] **Step 4: 提交**

```bash
git add scripts/build_kernel.sh
git commit -m "feat(M3): build_kernel.sh 产出 kernel.bin（asm.exe 汇编）"
```

---

### Task 4: kernel_boot_test.c + make test 挂载

**Files:**
- Create: `tests/kernel_boot_test.c`
- Modify: `Makefile`（all 行 13、test 目标 42-47、新增规则）

**Interfaces:**
- Consumes: `build/kernel.bin`（Task 3 产物）；`emu_run`（`emu/emu.h:35`：`EmuResult emu_run(const uint8_t *bin, size_t bin_len, size_t ram_size, uint64_t max_instr)`）
- Produces: 独立 exe，加入 `mingw32-make test` 全绿集（验收标准 1）

- [ ] **Step 1: 写测试（完整文件）**

```c
/* tests/kernel_boot_test.c — M3 验收：内核启动链端到端（模拟器）
 *
 * 读 build/kernel.bin（scripts/build_kernel.sh 产物，与游戏"外部文件"
 * 模式同源装载）→ emu_run（8MB 主存，同游戏）→ 断言帧缓冲 mem[0x2000]
 * 依次包含 11 行启动日志（设计文档 2026-08-21-m3-kernel-runtime-design.md）。
 * 帧缓冲约定：mem[0x2000] 起（emu/emu.h；与 crt0、config.h 一致）。
 * 停机：main() 返回 → crt0 尾部 "halt: jmp halt" 自跳 → emu_run 正常返回。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"

/* 11 行日志的完整屏幕输出（含 printk("\nBoot complete...") 前的空行） */
static const char EXPECTED[] =
	"mem_init OK\n"
	"trap_init OK (stub)\n"
	"blk_dev_init OK\n"
	"chr_dev_init OK\n"
	"tty_init OK\n"
	"time_init OK (stub)\n"
	"sched_init OK (stub)\n"
	"buffer_init OK\n"
	"hd_init OK (stub)\n"
	"floppy_init OK (stub)\n"
	"\nBoot complete. Halting.\n";

int main(void) {
	const char *path = "build/kernel.bin";
	FILE *fp = fopen(path, "rb");
	EmuResult res;
	static uint8_t bin[1 << 21];	/* kernel.bin ~570KB；大缓冲放 static（Windows 栈 1MB） */
	size_t len;
	size_t elen = strlen(EXPECTED);

	if (!fp) {
		fprintf(stderr, "FAIL: cannot open %s — run: bash scripts/build_kernel.sh\n", path);
		return 1;
	}
	len = fread(bin, 1, sizeof bin, fp);
	fclose(fp);
	if (len >= sizeof bin) {
		fprintf(stderr, "FAIL: %s too large for test buffer\n", path);
		return 1;
	}

	res = emu_run(bin, len, 8 << 20, 100000000);
	if (res.error) {
		fprintf(stderr, "FAIL: emu_run error=%d (1=max_instr, 2=addr) — 启动链打转或越界\n",
			res.error);
		free(res.mem);
		free(res.disk);
		return 1;
	}
	if (memcmp(EXPECTED, res.mem + 0x2000, elen) != 0) {
		fprintf(stderr, "FAIL: screen mismatch at 0x2000\n");
		fprintf(stderr, "  expected: %.*s\n  actual:   %.*s\n",
			(int)elen, EXPECTED, (int)elen, (const char *)res.mem + 0x2000);
		free(res.mem);
		free(res.disk);
		return 1;
	}
	printf("PASS kernel_boot (11-line boot log on screen)\n");
	free(res.mem);
	free(res.disk);
	return 0;
}
```

- [ ] **Step 2: Makefile 挂载**

`all` 行（13 行）追加目标：`... tests/link_test.exe tests/kernel_boot_test.exe`

`test` 目标（42-47 行）改为：

```make
build/kernel.bin:
	bash scripts/build_kernel.sh

test: all build/kernel.bin
	tests/asm_test.exe
	tests/emu_test.exe
	tests/run_tests.exe
	tests/preproc_test.exe
	tests/link_test.exe
	tests/kernel_boot_test.exe
```

新增编译规则（放在 `tests/link_test.exe` 规则后）：

```make
tests/kernel_boot_test.exe: tests/kernel_boot_test.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ tests/kernel_boot_test.c $(EMU_SRC)
```

说明：`build/kernel.bin` 规则仅在文件缺失时触发（无前置依赖，存在即最新）；缺失时 kernel_boot_test 也会给明确提示。

- [ ] **Step 3: 全量测试**

Run: `export PATH="/d/Downloads/msys64/mingw64/bin:$PATH" && mingw32-make test`
Expected: 6 个 exe 全部 PASS，末尾含 `PASS kernel_boot (11-line boot log on screen)`。

若 kernel_boot_test 失败：按 Step 1 的 FAIL 输出定位——`error=1` 查停机（Task 2 Step 4）；`error=2` 查越界（先怀疑栈：帧缓冲与栈间余量 4352 字节，启动链栈深应 <1KB）；screen mismatch 打印两段逐字符比对（常见原因：日志文本与计划不一致、或屏幕从非 0 位置起写）。

- [ ] **Step 4: 变异验证（证明断言有牙齿）**

把 `EXPECTED` 中 `"tty_init OK\n"` 临时改为 `"tty_init OK!\n"` → Run: `tests/kernel_boot_test.exe` → Expected: FAIL（screen mismatch）→ 改回 → Run: PASS。此步证明测试确实在断言屏幕内容。

- [ ] **Step 5: 提交**

```bash
git add tests/kernel_boot_test.c Makefile
git commit -m "test(M3): 内核启动链端到端测试（11 行日志断言）+ make test 挂载"
```

---

### Task 5: 游戏内验收 + README 里程碑更新

**Files:**
- Modify: `README.md`（徽章行 9、里程碑表 29-33、第 25 行说明、"在游戏里运行"章节后、文档列表）

**Interfaces:**
- Consumes: `build/kernel.bin`（Task 3 产物）
- Produces: 验收标准 3（游戏内人工验收）+ README 里程碑记录

- [ ] **Step 1: 游戏内验收（用户手动执行）**

1. 打开《Turing Complete》32-computer 架构，RAM 组件"外部文件"指向 `build/kernel.bin`
2. 运行程序
3. 预期：屏幕从左上角起逐行显示 11 行启动日志（`mem_init OK` … `Boot complete. Halting.`），光标停在第 11 行后，程序空转（halt 自循环），游戏 UI 不冻结
4. 验收即通过。若黑屏：先检查 RAM 端序 = Big endian；再确认屏幕组件数据偏移与 0x2000 一致（crt0 已设置命令 1=0x2000，除非组件配置被覆盖）
5. 红线确认：全程只走文件模式；绝不向"Edit assembly"粘贴 kernel.asm

- [ ] **Step 2: README 徽章行与里程碑表**

徽章行（9 行）改为：

```markdown
  M0 环境验证 [完成] &nbsp;|&nbsp; M1 编译器骨架 [完成]&nbsp;|&nbsp; M2 内核编译演练 [完成]&nbsp;|&nbsp; M3 内核运行期 [完成]
```

第 25 行说明改为：

```markdown
> M3 已完成：内核启动链在模拟器与游戏内跑通——11 行启动日志经 console 重定向
> 输出到游戏屏幕，fork 前停机。驱动/tick/调度归 M4/M5。
```

里程碑表（29-33 行）追加：

```markdown
| **M3** 内核运行期 | 完成（模拟器端到端 + 游戏内验收） | console 重定向到游戏屏（96×40 字节单元）、启动链逐阶段日志、fork 前停机、kernel.bin 外部文件模式加载 |
```

- [ ] **Step 3: README 新增"运行 Linux 0.11 内核"小节**

插在"在游戏里运行你的 C 程序"章节之后：

```markdown
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
```

- [ ] **Step 4: 文档列表补 M3 链接**

"文档"章节追加：

```markdown
- [M3 内核运行期验证设计](docs/superpowers/specs/2026-08-21-m3-kernel-runtime-design.md) —
  console 重定向、启动链日志、停机点与验收标准
```

- [ ] **Step 5: 校验无 emoji + 提交**

Run: `grep -P '[\x{1F300}-\x{1FAFF}]' README.md; echo $?`（grep 无匹配时输出非 0 即通过；也可目视检查）
Expected: 无 emoji 字符

```bash
git add README.md
git commit -m "docs: M3 里程碑 + 内核运行指南（模拟器/游戏内验收）"
```

---

## 自审

**Spec 覆盖：**
- 设计 §3 console 重定向（con_init 硬编码 / con_write 字节写 / 光标空操作 / 滚屏保留）→ Task 1 ✓
- 设计 §2 启动链与 11 行日志 → Task 2 ✓
- 设计 §4 停机点（`Boot complete. Halting.`）→ Task 2 Step 4 ✓（实现机制从 `for(;;)` 改为 main 返回 + crt0 尾部 jmp halt——Global Constraints 已记录实证原因；观察行为与设计一致：停在自循环、游戏不冻结）
- 设计 §5 构建管线 asm.exe → Task 3 ✓
- 设计 §6 模拟器端到端测试 + make test 挂载 → Task 4 ✓（max_instr 采用与 run_tests 相同的 100000000 安全上限；启动链实际指令数 < 100 万量级，与设计"实测取裕量"一致）
- 设计验收标准 1/2/3 → Task 4 / Task 3 / Task 5 ✓
- 沙盒红线 → Global Constraints + Task 5 Step 1 ✓

**占位符扫描：** 无 TBD/TODO/占位描述；每个步骤含具体代码或命令与预期输出。

**类型一致性：** 屏幕几何常量 0x2000/96/40 在 Task 1、Global Constraints 与 config.h 一致；11 行日志文本在 Task 2 与 Task 4 的 EXPECTED 逐字一致；`emu_run` 签名与 emu/emu.h 一致；`build/kernel.bin` 路径在 Task 3/4/5 一致。

**已知偏差（相对设计文档，均有实证）：**
1. 停机点实现：`for(;;)` → `main()` 返回 + crt0 尾部 `halt: jmp halt`（symld 桩化自跳转导致 for(;;) 无法停机，2026-08-23 实证）。观察行为与设计目标一致。
2. `time_init` 保留原样（CMOS 读全走 inb_p 桩返回 0，等效 stub），未额外加 stub 标记——与设计表"保留 stub"一致。
