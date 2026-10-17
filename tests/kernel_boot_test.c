/* tests/kernel_boot_test.c — M3 验收：内核启动链端到端（模拟器）
 *
 * 读 build/kernel.bin（scripts/build_kernel.sh 产物，与游戏"外部文件"
 * 模式同源装载）→ emu_run（8MB 主存，同游戏）→ 断言屏幕从 0x2000 起
 * 依次渲染 11 行启动日志（设计文档 2026-08-21-m3-kernel-runtime-design.md）。
 * 帧缓冲约定：mem[0x2000] 起、96 列/行（emu/emu.h；与 crt0、config.h 一致）。
 * 停机：main() 返回 → crt0 尾部 "halt: jmp halt" 自跳 → emu_run 正常返回。
 *
 * 屏幕比对说明：con_write 只把可打印字符落盘（\n 经 tty_write 的 O_NLCR
 * 转 \r\n 后由 cr()/lf() 处理，不写帧缓冲），行尾是 crt0 清屏的空格。
 * 故先把前 12 行（10 行日志 + "\nBoot..." 空行 + Boot 行——末条 \n 的
 * lf() 推进出的尾空行不算日志内容）裁掉行尾空格、以 \n 连接，再与
 * EXPECTED 逐字节比对。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"

/* 11 行日志的完整屏幕输出（含 printk("\nBoot complete...") 前的空行与
 * 结束换行：行尾换行由 tty 层 \n→\r\n + con_write cr()/lf() 产生） */
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

#define FB_BASE 0x2000
#define FB_COLS 96
#define N_ROWS  12	/* 11 日志行（含空行）+ Boot 行 */

int main(void) {
	const char *path = "build/m3/kernel.bin";
	FILE *fp = fopen(path, "rb");
	EmuResult res;
	static uint8_t bin[1 << 21];	/* kernel.bin ~570KB；大缓冲放 static（Windows 栈 1MB） */
	size_t len;
	size_t elen = strlen(EXPECTED);
	char actual[N_ROWS * (FB_COLS + 1)];
	size_t aoff = 0;
	int r;

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

	/* 逐行裁尾空格、以 \n 连接（行尾空格来自 crt0 清屏；换行不落盘） */
	for (r = 0; r < N_ROWS; r++) {
		const unsigned char *row = res.mem + FB_BASE + (size_t)FB_COLS * r;
		int e = FB_COLS - 1;
		while (e >= 0 && row[e] == ' ') e--;
		memcpy(actual + aoff, row, (size_t)e + 1);
		aoff += (size_t)e + 1;
		actual[aoff++] = '\n';
	}
	if (aoff != elen || memcmp(EXPECTED, actual, elen) != 0) {
		fprintf(stderr, "FAIL: screen mismatch at 0x2000 (rows 0..%d trimmed)\n", N_ROWS - 1);
		fprintf(stderr, "  expected (%u bytes):\n%.*s", (unsigned)elen, (int)elen, EXPECTED);
		fprintf(stderr, "  actual   (%u bytes):\n%.*s", (unsigned)aoff, (int)aoff, actual);
		free(res.mem);
		free(res.disk);
		return 1;
	}
	printf("PASS kernel_boot (11-line boot log on screen)\n");
	free(res.mem);
	free(res.disk);
	return 0;
}
