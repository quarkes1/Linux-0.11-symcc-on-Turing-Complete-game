#!/bin/bash
# scripts/build_kernel.sh — Linux 0.11 内核编译演练（M2 Task 8 验收）
# 用法: bash scripts/build_kernel.sh [clean]
# 流程: 50 个内核 .c → 50 个 .sym → symld 链接（crt0 + bss 清零 + jmp halt）→ kernel.asm
# 断言: 链接成功（J16 64KB 限制由链接器自校验）+ 输出含 main:/halt:
# SYMPLUS-PORT: tools/build.c 排除——宿主机打包工具（原版 Makefile 用宿主
# gcc 编译），其职责由本脚本自身取代，非目标机内核代码。
set -e
export PATH="/d/Downloads/msys64/mingw64/bin:$PATH"
cd "$(dirname "$0")/.."
K=Reference/Linux-0.11-main
OUT=build/kernel
if [ "$1" = clean ]; then
  rm -rf build
  exit 0
fi
mkdir -p "$OUT"
cd "$K"
CC=../../symcc/symcc.exe
LD=../../symld/symld.exe
INC="-I include"
DEFS="-D__GNUC__=2 -D__GNUC_MINOR__=7 -D__KERNEL__ -D__OPTIMIZE__"
n=0
# 编译每个 .c（init/kernel/mm/fs/lib 递归）
while IFS= read -r f; do
  base=$(echo "$f" | sed 's|^\./||' | tr '/' '_' | sed 's/\.c$/.sym/')
  echo "== [$n] $f"
  "$CC" $INC $DEFS -c "$f" -o "../../$OUT/$base"
  n=$((n+1))
done < <(find . -name '*.c' ! -path './tools/*' | sort)
echo "== compiled $n objects"
# 运行时库（有符号除/模——内核代码调用 __divsi3/__modsi3）
"$CC" -I include -c ../../runtime/divsi3.c -o "../../$OUT/runtime_divsi3.sym"
echo "== compiled runtime"
# 链接全部对象（crt0 显式指定，避免 cwd 依赖）
"$LD" --crt0 ../../runtime/crt0.asm -o ../../build/kernel.asm ../../build/kernel/*.sym
echo "== linked: $(wc -l < ../../build/kernel.asm) asm lines"
# 断言：入口与 halt 安全网存在
if ! grep -q '^main:' ../../build/kernel.asm; then
  echo "FAIL: no main: label in kernel.asm"; exit 1
fi
if ! grep -q '^halt:' ../../build/kernel.asm; then
  echo "FAIL: no halt: label in kernel.asm"; exit 1
fi
echo "KERNEL BUILD OK"
