#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/user
python scripts/gen_platform.py
symcc/symcc.exe --d32 -I user/include -c user/lib/libc.c -o build/user/libc.sym
symcc/symcc.exe --d32 -c runtime/divsi3.c -o build/user/divsi3.sym
symcc/symcc.exe --d32 -I user/include -c user/lib/shell_parse.c -o build/user/shell_parse.sym
for source in user/bin/*.c; do
    name=$(basename "$source" .c)
    extra=()
    if [ "$name" = sh ]; then extra+=(build/user/shell_parse.sym); fi
    symcc/symcc.exe --d32 -I user/include -I runtime -c "$source" -o "build/user/$name.sym"
    symld/symld.exe --image user --crt0 user/lib/crt0.sym user/lib/syscall.sym build/user/libc.sym build/user/divsi3.sym "${extra[@]}" "build/user/$name.sym" -o "build/user/$name.asm" --bin "build/user/$name.spx"
done
