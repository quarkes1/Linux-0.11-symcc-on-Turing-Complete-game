#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/native
cc=(symcc/symcc.exe --d32 -I user/native/include -I user/include -I runtime)
"${cc[@]}" -Dputs=sp_raw_puts -c user/lib/libc.c -o build/native/base.sym
for source in heap stdio format string convert; do
    "${cc[@]}" -c "user/native/$source.c" -o "build/native/$source.sym"
done
cp user/lib/crt0.sym build/native/crt0.sym
cp user/lib/syscall.sym build/native/syscall.sym
cp build/user/divsi3.sym build/native/divsi3.sym
"${cc[@]}" -c tests/native_libc_probe.c -o build/native/sdkprobe.sym
symld/symld.exe --image user --crt0 user/lib/crt0.sym user/lib/syscall.sym build/native/{base,heap,stdio,format,string,convert,divsi3,sdkprobe}.sym -o build/native/sdkprobe.asm --bin build/user/sdkprobe.spx
