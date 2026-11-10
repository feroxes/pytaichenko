#!/usr/bin/env bash
set -euo pipefail

firmware_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(dirname "$(dirname "$firmware_dir")")"
build_dir="$firmware_dir/build"
out_dir="$project_dir/site/firmware/v2"
dist_dir="$build_dir/dist"

usage() {
  echo "usage: build-web.sh [--manifest-only] [--version X.Y.Z] [--out DIR] [--dist DIR] [--from-factory FILE]" >&2
  exit 2
}

fail() {
  echo "$1" >&2
  exit 1
}

manifest_only=0
version_given=0
version=""
dist_given=0
factory=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --manifest-only)
      manifest_only=1
      shift
      ;;
    --version)
      [ "$#" -ge 2 ] || usage
      version="$2"
      version_given=1
      shift 2
      ;;
    --out)
      [ "$#" -ge 2 ] && [ -n "$2" ] || usage
      out_dir="$2"
      shift 2
      ;;
    --dist)
      [ "$#" -ge 2 ] && [ -n "$2" ] || usage
      dist_dir="$2"
      dist_given=1
      shift 2
      ;;
    --from-factory)
      [ "$#" -ge 2 ] && [ -n "$2" ] || usage
      factory="$2"
      shift 2
      ;;
    *)
      usage
      ;;
  esac
done

if [ -n "$factory" ]; then
  [ "$manifest_only" -eq 0 ] && [ "$dist_given" -eq 0 ] || usage
  [ "$version_given" -eq 1 ] || fail "--from-factory needs --version X.Y.Z"
elif [ "$manifest_only" -eq 1 ] && [ "$dist_given" -eq 1 ]; then
  usage
fi

if [ "$version_given" -eq 0 ]; then
  [ -f "$firmware_dir/VERSION" ] || fail "missing $firmware_dir/VERSION"
  version="$(head -n 1 "$firmware_dir/VERSION" | tr -d '\r')"
  version="${version%%-*}"
fi
if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  fail "version must look like 2.0.0, got '$version'"
fi

if command -v python3 > /dev/null; then
  python="python3"
elif command -v python > /dev/null; then
  python="python"
else
  fail "python not found: run . ~/esp/esp-idf/export.sh first"
fi

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
tool=("$python" "$firmware_dir/tools/web_image.py")
common=(--csv "$firmware_dir/partitions.csv" --sdkconfig "$firmware_dir/sdkconfig.defaults"
  --version "$version" --out "$stage/site")

if [ -n "$factory" ]; then
  [ -f "$factory" ] || fail "missing $factory"
  "${tool[@]}" split "${common[@]}" --image "$factory" || exit 1
elif [ "$manifest_only" -eq 1 ]; then
  "${tool[@]}" manifest "${common[@]}" || exit 1
else
  [ -f "$build_dir/flasher_args.json" ] || fail "no build at $build_dir: run idf.py -C v2/firmware build first"
  if command -v esptool.py > /dev/null; then
    esptool="esptool.py"
  elif command -v esptool > /dev/null; then
    esptool="esptool"
  else
    fail "esptool not found: run . ~/esp/esp-idf/export.sh first"
  fi
  "${tool[@]}" full "${common[@]}" --build "$build_dir" --esptool "$esptool" --dist "$stage/dist" || exit 1
fi

mkdir -p "$out_dir"
if [ -f "$out_dir/manifest.json" ]; then
  "$python" -I -c '
import json
import re
import sys

try:
    with open(sys.argv[1]) as handle:
        builds = json.load(handle).get("builds") or []
    for build in builds:
        for part in build.get("parts") or []:
            name = part.get("path")
            if isinstance(name, str) and re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*\.bin", name):
                print(name)
except Exception:
    pass
' "$out_dir/manifest.json" | while IFS= read -r name; do
    rm -f "$out_dir/$name"
  done
fi
for name in manifest.json bootloader.bin partition-table.bin ota_data_initial.bin pytaichenko.bin pytaichenko.factory.bin pytaichenko.ota.bin; do
  rm -f "$out_dir/$name"
done
cp "$stage/site/"* "$out_dir/"
if [ -d "$stage/dist" ]; then
  mkdir -p "$dist_dir"
  cp "$stage/dist/"* "$dist_dir/"
fi
