# Makefile — SymphonyPlus 工具链（emu/）
# 注意：需以 PATH 前缀调用（MSYS2 优先于 Git Bash 自带 /mingw64）：
#   PATH="/d/Downloads/msys64/mingw64/bin:$PATH" mingw32-make

CC := gcc
CFLAGS := -O0 -g -Wall -Wextra -I.

EMU_SRC := emu/isa.c emu/asm.c emu/emu.c
EMU_HDR := emu/isa.h emu/asm.h emu/emu.h
SYMCC_SRC := symcc/src/main.c symcc/src/tokenize.c symcc/src/parse.c symcc/src/codegen.c symcc/src/compile.c
SYMCC_LIB := symcc/src/tokenize.c symcc/src/parse.c symcc/src/codegen.c symcc/src/compile.c

all: emu/asm.exe emu/emu.exe tests/asm_test.exe tests/emu_test.exe symcc/symcc.exe tests/run_tests.exe

symcc/symcc.exe: $(SYMCC_SRC) symcc/src/symcc.h
	$(CC) $(CFLAGS) -o $@ $(SYMCC_SRC)

tests/run_tests.exe: tests/run_tests.c $(EMU_SRC) $(EMU_HDR) $(SYMCC_LIB) symcc/src/symcc.h runtime/divsi3.c runtime/tty.c runtime/crt0.asm symcc/include/config.h
	$(CC) $(CFLAGS) -o $@ tests/run_tests.c $(EMU_SRC) $(SYMCC_LIB)

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
	tests/run_tests.exe

clean:
	rm -f emu/*.exe tests/*.exe symcc/symcc.exe

.PHONY: all test clean
