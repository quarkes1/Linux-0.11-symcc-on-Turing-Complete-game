#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/user
python scripts/gen_platform.py
symcc/symcc.exe --d32 -I user/include -c user/lib/libc.c -o build/user/libc.sym
symcc/symcc.exe --d32 -c runtime/divsi3.c -o build/user/divsi3.sym
for source in user/bin/*.c; do
    name=$(basename "$source" .c)
    symcc/symcc.exe --d32 -I user/include -c "$source" -o "build/user/$name.sym"
    symld/symld.exe --image user --crt0 user/lib/crt0.sym user/lib/syscall.sym build/user/libc.sym build/user/divsi3.sym "build/user/$name.sym" -o "build/user/$name.asm" --bin "build/user/$name.spx"
done
