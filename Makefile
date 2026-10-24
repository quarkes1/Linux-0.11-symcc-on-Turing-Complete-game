# Makefile — SymphonyPlus 工具链（emu/）
# 注意：需以 PATH 前缀调用（MSYS2 优先于 Git Bash 自带 /mingw64）：
#   PATH="/d/Downloads/msys64/mingw64/bin:$PATH" mingw32-make

CC := gcc
CFLAGS := -O0 -g -Wall -Wextra -I.

EMU_SRC := emu/isa.c emu/asm.c emu/emu.c
EMU_HDR := emu/isa.h emu/asm.h emu/emu.h
PLATFORM_HDR := runtime/platform.h runtime/compiler_int.h symcc/include/config.h symcc/src/obj.h symcc/src/link.h symcc/src/symcc.h symcc/src/const_int.h
SYMCC_SRC := symcc/src/main.c symcc/src/tokenize.c symcc/src/parse.c symcc/src/codegen.c symcc/src/compile.c symcc/src/preprocess.c symcc/src/obj.c symcc/src/link.c symcc/src/link_image.c
SYMCC_LIB := symcc/src/tokenize.c symcc/src/parse.c symcc/src/codegen.c symcc/src/compile.c symcc/src/preprocess.c symcc/src/obj.c symcc/src/link.c symcc/src/link_image.c

all: emu/asm.exe emu/emu.exe tests/asm_test.exe tests/emu_test.exe symcc/symcc.exe symld/symld.exe tests/run_tests.exe tests/preproc_test.exe tests/link_test.exe tests/kernel_boot_test.exe

symcc/symcc.exe: $(SYMCC_SRC) symcc/src/symcc.h $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ $(SYMCC_SRC) $(EMU_SRC)

tests/preproc_test.exe: tests/preproc_test.c symcc/src/tokenize.c symcc/src/preprocess.c symcc/src/symcc.h
	$(CC) $(CFLAGS) -o $@ tests/preproc_test.c symcc/src/tokenize.c symcc/src/preprocess.c

tests/run_tests.exe: tests/run_tests.c $(EMU_SRC) $(EMU_HDR) $(SYMCC_LIB) symcc/src/symcc.h runtime/divsi3.c runtime/tty.c runtime/crt0.asm symcc/include/config.h
	$(CC) $(CFLAGS) -o $@ tests/run_tests.c $(EMU_SRC) $(SYMCC_LIB)

tests/link_test.exe: tests/link_test.c $(SYMCC_LIB) $(EMU_SRC) $(EMU_HDR) symcc/src/symcc.h symcc/src/link.h
	$(CC) $(CFLAGS) -o $@ tests/link_test.c $(SYMCC_LIB) $(EMU_SRC)

tests/kernel_boot_test.exe: tests/kernel_boot_test.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ tests/kernel_boot_test.c $(EMU_SRC)

symld/symld.exe: symld/symld.c symcc/src/obj.c symcc/src/link.c symcc/src/link_image.c symcc/src/obj.h symcc/src/link.h $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ symld/symld.c symcc/src/obj.c symcc/src/link.c symcc/src/link_image.c $(EMU_SRC)

emu/asm.exe: emu/asm_main.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ emu/asm_main.c $(EMU_SRC)

emu/emu.exe: emu/emu_main.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -O2 -o $@ emu/emu_main.c $(EMU_SRC)

tests/asm_test.exe: tests/asm_test.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ tests/asm_test.c $(EMU_SRC)

tests/emu_test.exe: tests/emu_test.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ tests/emu_test.c $(EMU_SRC)

test: all m3-image system-image
	tests/asm_test.exe
	tests/emu_test.exe
	tests/run_tests.exe
	tests/preproc_test.exe
	tests/link_test.exe
	tests/kernel_boot_test.exe
	tests/platform_test.exe
	tests/bridge_copy_test.exe
	tests/capacity_probe_test.exe
	tests/emu_io_test.exe
	tests/user_link_test.exe
	tests/shell_test.exe
	tests/shell_test.exe --process
	tests/shell_test.exe --stress
	tests/shell_test.exe --tty
	tests/shell_test.exe --ui
	tests/shell_test.exe --keys
	tests/shell_test.exe --symbols
	tests/shell_test.exe --files
	tests/shell_test.exe --lists
	tests/shell_test.exe --reset
	tests/shell_test.exe --interrupt
	tests/shell_test.exe --interrupt-native
	tests/shell_test.exe --interrupt-compiler
	tests/shell_test.exe --interrupt-publish
	tests/shell_test.exe --interrupt-builtin
	tests/shell_parse_test.exe
	tests/native_heap_test.exe
	tests/tick_test.exe
	python -m unittest discover -s tests -p test_mkrootfs.py
	python -m unittest discover -s tests -p test_platform_config.py
	python -m unittest discover -s tests -p test_disk_capacity.py
	python -m unittest discover -s tests -p test_native_runtime.py
	python -m unittest discover -s tests -p test_native_compiler.py
	python -m unittest discover -s tests -p test_emu_cli.py

m3-image: all
	bash scripts/build_kernel.sh --m3

system-image: all
	bash scripts/build_system.sh
	bash scripts/build_capacity_probe.sh

clean:
	rm -f emu/*.exe tests/*.exe symcc/symcc.exe symld/symld.exe

.PHONY: all test clean m3-image system-image

symcc/symcc.exe symld/symld.exe tests/run_tests.exe tests/link_test.exe tests/user_link_test.exe: $(PLATFORM_HDR)

all: tests/platform_test.exe tests/emu_io_test.exe tests/user_link_test.exe tests/shell_test.exe
all: tests/tick_test.exe
all: tests/shell_parse_test.exe
all: tests/bridge_copy_test.exe
all: tests/capacity_probe_test.exe
all: tests/disk_capacity_test.exe
all: tests/native_heap_test.exe

tests/native_heap_test.exe: tests/native_heap_test.c user/native/heap.c
	$(CC) $(CFLAGS) -O2 -o $@ tests/native_heap_test.c

tests/disk_capacity_test.exe: tests/disk_capacity_test.c emu/emu.c $(EMU_HDR) runtime/platform.h
	$(CC) $(CFLAGS) -O2 -o $@ tests/disk_capacity_test.c emu/emu.c

tests/capacity_probe_test.exe: tests/capacity_probe_test.c $(EMU_SRC) $(EMU_HDR) runtime/platform.h
	$(CC) $(CFLAGS) -o $@ tests/capacity_probe_test.c $(EMU_SRC)

tests/bridge_copy_test.exe: tests/bridge_copy_test.c $(SYMCC_LIB) $(EMU_SRC) $(PLATFORM_HDR)
	$(CC) $(CFLAGS) -o $@ tests/bridge_copy_test.c $(SYMCC_LIB) $(EMU_SRC)

tests/shell_parse_test.exe: tests/shell_parse_test.c user/lib/shell_parse.c user/include/shell.h
	$(CC) $(CFLAGS) -Iuser/include -o $@ tests/shell_parse_test.c user/lib/shell_parse.c

tests/tick_test.exe: tests/tick_test.c Reference/Linux-0.11-main/kernel/symplus/tick.c
	$(CC) $(CFLAGS) -o $@ $^

tests/platform_test.exe: tests/platform_test.c runtime/platform.h
	$(CC) $(CFLAGS) -o $@ $<

tests/emu_io_test.exe: tests/emu_io_test.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ $< $(EMU_SRC)

tests/user_link_test.exe: tests/user_link_test.c $(SYMCC_LIB) $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ tests/user_link_test.c $(SYMCC_LIB) $(EMU_SRC)

tests/shell_test.exe: tests/shell_test.c emu/emu.c emu/emu.h runtime/platform.h
	$(CC) $(CFLAGS) -O2 -o $@ tests/shell_test.c emu/emu.c
