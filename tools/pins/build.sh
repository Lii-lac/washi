#!/bin/sh
# Makes data/pins.bin (the push pin's pictures) and tools/pins/out/sheet.png (all of them, for a look). Run in WSL from anywhere:
#   sh tools/pins/build.sh
# Needs gcc with OpenMP; takes a minute or two on many cores.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../.." && pwd)
mkdir -p "$HERE/out"
cd "$TOP/source/gfx"
gcc -O2 -fopenmp -Wall -Wno-unused-function -I. -I"$HERE" -I"$TOP/source" -o "$HERE/out/make_pins" \
    "$HERE/make_pins.c" "$HERE/pp.c" paper.c crayon.c gfx.c fontdata.c -lm
"$HERE/out/make_pins" "$TOP/data/pins.bin" "$HERE/out/sheet.png"
ls -l "$TOP/data/pins.bin"
