#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/capacity
symcc/symcc.exe --d32 -I runtime -c runtime/capacity_probe.c -o build/capacity/probe.sym
symld/symld.exe --image kernel --crt0 runtime/empty_crt0.asm runtime/capacity_crt0.sym build/capacity/probe.sym -o build/capacity/probe.asm --bin build/capacity-probe.bin
python scripts/platform_config.py --pad-kernel build/capacity-probe.bin
python -c 'from pathlib import Path; from scripts.platform_config import read_config; p=Path("build/capacity-disk.bin"); f=p.open("wb"); f.truncate(read_config()["SP_DISK_BYTES"]); f.close()'
