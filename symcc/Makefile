# symcc/Makefile — SymphonyPlus C 编译器
# 注意：需以 PATH 前缀调用（MSYS2 优先）：见顶层 Makefile

CC := gcc
CFLAGS := -O0 -g -Wall -Wextra -I..

SRCS := src/main.c src/tokenize.c src/parse.c src/codegen.c src/compile.c src/preprocess.c

symcc.exe: $(SRCS) src/symcc.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f symcc.exe

.PHONY: clean
