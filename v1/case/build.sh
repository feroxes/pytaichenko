#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

OPENSCAD="${OPENSCAD:-/opt/homebrew/bin/openscad}"
BACKEND="--backend=manifold"

PARTS=(body:PETG-black_body ring_arc:PETG-white_ring_arc_x2 tray:PETG-black_tray tray_gasket:TPU_tray_gasket button_cap:PETG-black_button_cap foot:TPU_foot_x4 top:PETG-black_top speaker_gasket:TPU_speaker_gasket top_plug:TPU_top_plug_x2)

if ! command -v "$OPENSCAD" >/dev/null 2>&1; then
    echo "OpenSCAD not found at $OPENSCAD: set OPENSCAD to the path of the openscad binary" >&2
    exit 1
fi

mkdir -p stl png
if [ ! -w stl ] || [ ! -w png ]; then
    echo "v1/case/stl and v1/case/png must be writable" >&2
    exit 1
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/voice-case.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/stl" "$work/png"

for e in ${PARTS[@]+"${PARTS[@]}"}; do
    p="${e%%:*}"
    f="${e#*:}"
    "$OPENSCAD" "$BACKEND" -D "part=\"$p\"" --export-format binstl -o "$work/stl/$f.stl" voice-case.scad
    "$OPENSCAD" "$BACKEND" -D "part=\"$p\"" --render --autocenter --viewall \
        --camera=0,0,0,62,0,325,0 --imgsize=1200,900 --colorscheme=Tomorrow \
        -o "$work/png/$p.png" voice-case.scad
done

"$OPENSCAD" "$BACKEND" -D 'part="assembly"' --preview --autocenter --viewall \
    --camera=0,0,50,62,0,325,0 --imgsize=1600,1200 --colorscheme=Tomorrow \
    -o "$work/png/assembly.png" voice-case.scad

rm -f stl/*.stl
mv "$work"/stl/*.stl stl/
mv "$work"/png/*.png png/
