# Makefile — SymphonyPlus 工具链（emu/）
# 注意：需以 PATH 前缀调用（MSYS2 优先于 Git Bash 自带 /mingw64）：
#   PATH="/d/Downloads/msys64/mingw64/bin:$PATH" mingw32-make

CC := gcc
CFLAGS := -O0 -g -Wall -Wextra -I.

EMU_SRC := emu/isa.c emu/asm.c emu/emu.c
EMU_HDR := emu/isa.h emu/asm.h emu/emu.h

all: emu/asm.exe emu/emu.exe tests/asm_test.exe tests/emu_test.exe

emu/asm.exe: emu/asm_main.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ emu/asm_main.c $(EMU_SRC)

emu/emu.exe: emu/emu_main.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ emu/emu_main.c $(EMU_SRC)

tests/asm_test.exe: tests/asm_test.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ tests/asm_test.c $(EMU_SRC)

tests/emu_test.exe: tests/emu_test.c $(EMU_SRC) $(EMU_HDR)
	$(CC) $(CFLAGS) -o $@ tests/emu_test.c $(EMU_SRC)

test: all
	tests/asm_test.exe
	tests/emu_test.exe

clean:
	rm -f emu/*.exe tests/*.exe

.PHONY: all test clean
