#!/bin/bash
# Draws the app's own progress roll (source/gfx/paper_roll.c) as the README's tape divider: a straight tape laid to the right edge, once on white and once on
# black, in each theme. build.py turns each pair into one transparent PNG. Needs gcc; run from anywhere (it moves to the repository root).
#   bash docs/readme/roll.sh <output folder, relative to the repository root>
set -e
cd "$(dirname "$0")/../.."
OUT=${1:?usage: roll.sh <output folder>}
mkdir -p "$OUT"
BIN=$(mktemp)
trap 'rm -f "$BIN"' EXIT
G=source/gfx
gcc -std=gnu11 -O2 -w -I$G -o "$BIN" docs/readme/rolldiv.c $G/paper_roll.c $G/paper.c $G/gfx.c $G/gfx_image.c $G/fontdata.c -lm
"$BIN" 0 1259 0 0 4242 "$OUT/L"
"$BIN" 1 1259 0 0 4242 "$OUT/D"
