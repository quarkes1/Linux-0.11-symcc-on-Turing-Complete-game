#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/interactive
python scripts/gen_platform.py
bash scripts/build_user.sh
python scripts/mkrootfs.py
objects=()
while IFS= read -r source; do
    name=${source#Reference/Linux-0.11-main/}
    name=${name//\//_}
    name=${name%.c}.sym
    symcc/symcc.exe --d32 -I Reference/Linux-0.11-main/include -I runtime -DSP_INTERACTIVE -D__GNUC__=2 -D__GNUC_MINOR__=7 -D__KERNEL__ -D__OPTIMIZE__ -c "$source" -o "build/interactive/$name"
    objects+=("build/interactive/$name")
done < <(find Reference/Linux-0.11-main -name '*.c' ! -path '*/tools/*' | sort)
symcc/symcc.exe --d32 -c runtime/divsi3.c -o build/interactive/divsi3.sym
symld/symld.exe --image kernel --crt0 runtime/crt0_interactive.asm runtime/platform.sym "${objects[@]}" build/interactive/divsi3.sym -o build/kernel.asm --bin build/kernel.bin
python scripts/platform_config.py --pad-kernel build/kernel.bin
echo 'INTERACTIVE SYSTEM BUILD OK'
