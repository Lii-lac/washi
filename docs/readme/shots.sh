#!/bin/bash
# Renders the screenshots the README pictures use, from the app's own PC build (the same C code the console runs).
#   wsl bash docs/readme/shots.sh <output folder>        (needs gcc; run from the repository root)
#   WASHI_REPO=<other checkout> ...                      (to render from a different checkout than the one holding this script)
# With data/avatars.bin in place (docs/profile-pictures.md) the people show the Switch's pictures; without it they are letters.
# It works on a private copy of source/, data/ and test/, so nothing in the repository is changed or rebuilt.
# Output: home.png, swap.png, backups.png and the same three with -dark (Paper night), 1280 x 720. `python docs/readme/build.py --import <folder>` turns them into
# the JPEGs in docs/readme/shots/.
set -e
OUT=$(realpath -m "${1:?usage: shots.sh <output folder>}")
REPO=${WASHI_REPO:-$(cd "$(dirname "$0")/../.." && pwd)}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/test" "$OUT"
cp -r "$REPO/source" "$REPO/data" "$WORK/"
cp "$REPO/Makefile" "$WORK/"
(cd "$REPO/test" && cp *.c *.h Makefile.app "$WORK/test/")
cd "$WORK/test"
# The scenario's own "swap" picture is taken as the bar starts (0%). SWAP_FRAMES frames later the roll is part-way along.
FR=${SWAP_FRAMES:-40}
sed -i "s/hp_snap_after(h, 5, \"swap\");/hp_snap_after(h, $FR, \"swap\");/" app_host.c
grep -q "hp_snap_after(h, $FR, \"swap\")" app_host.c || { echo "the readme-shots scenario changed: update the sed line in shots.sh"; exit 1; }
make -f Makefile.app CFLAGS="-std=gnu11 -O2 -w" app_host >/dev/null
./app_host readme-shots >/dev/null
cp out/app/40-readme-shots/01-home.png "$OUT/home.png"
cp out/app/40-readme-shots/03-swap.png "$OUT/swap.png"
cp out/app/40-readme-shots/08-backups.png "$OUT/backups.png"
rm -rf out
DARK=1 ./app_host readme-shots >/dev/null
cp out/app-dark/40-readme-shots/01-home.png "$OUT/home-dark.png"
cp out/app-dark/40-readme-shots/03-swap.png "$OUT/swap-dark.png"
cp out/app-dark/40-readme-shots/08-backups.png "$OUT/backups-dark.png"
echo "wrote home, swap and backups, each as .png and -dark.png, to $OUT"
