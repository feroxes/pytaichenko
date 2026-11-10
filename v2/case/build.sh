#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

OPENSCAD="${OPENSCAD:-/opt/homebrew/bin/openscad}"
BACKEND="--backend=manifold"

PARTS=(
    "top:PETG-black_top"
    "body:PETG-black_body"
    "ring_arc:PETG-white_ring_arc"
    "tray:PETG-black_tray"
    "button_cap:PETG-black_button_cap"
    "speaker_gasket:TPU_speaker_gasket"
    "top_gasket:TPU_top_gasket"
    "tray_gasket:TPU_tray_gasket"
    "top_plug:PETG-black_top_plug_x3"
    "foot:TPU_foot_x4"
    "com_bushing:TPU_com_bushing"
)

if ! command -v "$OPENSCAD" >/dev/null 2>&1; then
    echo "OpenSCAD not found at $OPENSCAD: set OPENSCAD to the path of the openscad binary" >&2
    exit 1
fi

mkdir -p stl png
if [ ! -w stl ] || [ ! -w png ]; then
    echo "v2/case/stl and v2/case/png must be writable" >&2
    exit 1
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/pytaichenko-case.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/stl" "$work/png"

for e in ${PARTS[@]+"${PARTS[@]}"}; do
    p="${e%%:*}"
    f="${e#*:}"
    "$OPENSCAD" "$BACKEND" -D "part=\"$p\"" --export-format binstl -o "$work/stl/$f.stl" pytaichenko-case.scad
    "$OPENSCAD" "$BACKEND" -D "part=\"$p\"" --render --autocenter --viewall \
        --camera=0,0,0,62,0,325,0 --imgsize=1200,900 --colorscheme=Tomorrow \
        -o "$work/png/$p.png" pytaichenko-case.scad
done

"$OPENSCAD" "$BACKEND" -D 'part="assembly"' --preview --autocenter --viewall \
    --camera=0,0,30,62,0,325,0 --imgsize=1600,1200 --colorscheme=Tomorrow \
    -o "$work/png/assembly.png" pytaichenko-case.scad

"$OPENSCAD" "$BACKEND" -D 'part="asm_top"' --render --autocenter --viewall --projection=o \
    --camera=0,0,0,0,0,0,200 --imgsize=1200,1200 --colorscheme=Tomorrow \
    -o "$work/png/top_face.png" pytaichenko-case.scad
"$OPENSCAD" "$BACKEND" -D 'part="assembly"' -D 'lid_alpha=1' --preview --autocenter --viewall \
    --camera=0,0,30,62,0,325,0 --imgsize=1600,1200 --colorscheme=Tomorrow \
    -o "$work/png/assembly_closed.png" pytaichenko-case.scad

rm -f stl/*.stl
if compgen -G "$work/stl/*.stl" >/dev/null; then
    mv "$work"/stl/*.stl stl/
fi
mv "$work"/png/*.png png/
