#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

OPENSCAD="${OPENSCAD:-/opt/homebrew/bin/openscad}"
OUT="../../site/assets/renders/v2"

CAMERAS=(
    "0,0,10,45,0,20,200"
    "15,0,8,50,0,200,190"
    "-15,0,8,45,0,20,190"
    "0,0,12,45,0,20,220"
    "0,0,26,45,0,200,220"
    "0,0,45,50,0,20,230"
    "0,22,24,55,0,0,210"
    "0,22,24,55,0,0,210"
    "0,15,24,50,0,0,200"
    "0,0,55,45,0,180,230"
    "0,0,55,45,0,180,230"
    "0,0,45,70,0,200,330"
    "0,0,62,45,0,20,240"
    "0,0,62,45,0,20,240"
    "2,-28,18,64,0,30,280"
    "0,0,10,45,0,20,180"
    "0,-28,8,50,0,0,140"
    "0,-28,8,50,0,0,140"
    "0,-36,8,30,0,0,120"
    "0,0,12,45,0,20,200"
    "0,0,34,45,0,20,290"
    "0,0,22,45,0,20,200"
    "0,0,16,45,0,20,240"
    "0,0,55,60,0,200,300"
    "0,0,60,45,0,200,230"
    "0,0,60,45,0,200,230"
)

mkdir -p "$OUT"
rm -f "$OUT"/assembly-*.png

for n in $(seq 1 ${#CAMERAS[@]}); do
    "$OPENSCAD" --backend=manifold --render -D "step=$n" \
        --camera="${CAMERAS[$((n - 1))]}" --imgsize=1000,750 --colorscheme=Tomorrow \
        -o "$OUT/assembly-$(printf '%02d' "$n").png" steps.scad
done

"$OPENSCAD" --backend=manifold --render \
    --camera=0,0,26,66,0,205,250 --imgsize=1200,900 --colorscheme=Tomorrow \
    -o "$OUT/hero.png" hero.scad
