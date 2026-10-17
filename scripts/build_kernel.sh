#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
if [ "${1:-}" != --m3 ]; then
    exec bash scripts/build_system.sh
fi
mkdir -p build/m3/objects
objects=()
while IFS= read -r source; do
    name=${source#Reference/Linux-0.11-main/}
    name=${name//\//_}
    name=${name%.c}.sym
    symcc/symcc.exe --d32 -I Reference/Linux-0.11-main/include -D__GNUC__=2 -D__GNUC_MINOR__=7 -D__KERNEL__ -D__OPTIMIZE__ -c "$source" -o "build/m3/objects/$name"
    objects+=("build/m3/objects/$name")
done < <(find Reference/Linux-0.11-main -name '*.c' ! -path '*/tools/*' ! -path '*/kernel/symplus/*' ! -name 'symdisk.c' ! -name 'symexec.c' ! -name 'minix_disk.c' | sort)
symcc/symcc.exe --d32 -c runtime/divsi3.c -o build/m3/objects/divsi3.sym
symld/symld.exe --image kernel --crt0 runtime/crt0_interactive.asm "${objects[@]}" build/m3/objects/divsi3.sym -o build/m3/kernel.asm --bin build/m3/kernel.bin
echo 'M3 KERNEL BUILD OK'
