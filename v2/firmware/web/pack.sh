#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../build/web}
mkdir -p "$out"
for name in index.html app.js style.css favicon.svg; do
    gzip -9n -c "$here/$name" > "$out/$name.gz"
done
