#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
cc=(symcc/symcc.exe --d32 -DSP_NATIVE -I user/native/include -I user/include -I runtime -I symcc/src)
for source in tokenize preprocess parse codegen obj link_image; do
    echo "native core: $source"
    "${cc[@]}" -c "symcc/src/$source.c" -o "build/native/$source.sym"
done
for source in asm isa; do
    "${cc[@]}" -c "emu/$source.c" -o "build/native/$source.sym"
done
"${cc[@]}" -c tests/native_core_probe.c -o build/native/coreprobe.sym
symld/symld.exe --image user --crt0 user/lib/crt0.sym user/lib/syscall.sym build/native/{base,heap,stdio,format,string,convert,divsi3,tokenize,coreprobe}.sym -o build/native/coreprobe.asm --bin build/user/coreprobe.spx
runtime=(user/lib/syscall.sym build/native/{base,heap,stdio,format,string,convert,divsi3}.sym)
for stage in cpp cc1 ld; do
    "${cc[@]}" -c "user/native/${stage}_main.c" -o "build/native/${stage}_main.sym"
    case "$stage" in
        cpp) name=scpp; core=(tokenize preprocess) ;;
        cc1) name=scc1; core=(tokenize parse codegen obj) ;;
        ld) name=sld; core=(obj link_image asm isa) ;;
    esac
    objects=();for part in "${core[@]}";do objects+=("build/native/$part.sym");done
    symld/symld.exe --image user --crt0 user/lib/crt0.sym "${runtime[@]}" "${objects[@]}" "build/native/${stage}_main.sym" -o "build/native/$name.asm" --bin "build/user/$name.spx"
done
