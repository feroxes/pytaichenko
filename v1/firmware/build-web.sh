#!/usr/bin/env bash
set -euo pipefail

firmware_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(dirname "$(dirname "$firmware_dir")")"
out_dir="${WEB_OUT_DIR:-$project_dir/site/firmware/v1}"
factory_bin="${FACTORY_BIN:-$firmware_dir/.esphome/build/pytaichenko/build/firmware.factory.bin}"

usage() {
  echo "usage: build-web.sh [--manifest-only [--version X.Y.Z]] [--out DIR]" >&2
  exit 2
}

manifest_only=0
version_given=0
release_version=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --manifest-only)
      manifest_only=1
      shift
      ;;
    --version)
      [ "$#" -ge 2 ] || usage
      release_version="$2"
      version_given=1
      shift 2
      ;;
    --out)
      [ "$#" -ge 2 ] && [ -n "$2" ] || usage
      out_dir="$2"
      shift 2
      ;;
    *)
      usage
      ;;
  esac
done
if [ "$version_given" -eq 1 ]; then
  [ "$manifest_only" -eq 1 ] || usage
  if ! [[ "$release_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "version must look like 1.0.0, got '$release_version'" >&2
    exit 1
  fi
fi

project_field() {
  awk -v field="$1" '
    /^[^[:space:]#]/ { top = $0; key = ""; next }
    top == "esphome:" && /^  [^[:space:]#]/ { key = $1; next }
    top == "esphome:" && key == "project:" && $1 == field ":" {
      sub(/\r$/, "")
      sub(/^[^:]*:[[:space:]]*/, "")
      quote = substr($0, 1, 1)
      if (quote == "\"" || quote == "\047") {
        rest = substr($0, 2)
        end = index(rest, quote)
        print (end > 0 ? substr(rest, 1, end - 1) : "")
      } else {
        sub(/[[:space:]]+#.*$/, "")
        sub(/[[:space:]]+$/, "")
        print
      }
      exit
    }
  ' "$firmware_dir/pytaichenko.yaml"
}
name="$(project_field name)"
version="$(project_field version)"
if [ -z "$name" ] || [ -z "$version" ]; then
  echo "v1/firmware/pytaichenko.yaml must set esphome: project: name and version" >&2
  exit 1
fi
if [ "$version_given" -eq 1 ]; then
  version="$release_version"
fi

if [ "$manifest_only" -eq 0 ]; then
  if [ -n "${ESPHOME_BIN:-}" ]; then
    esphome="$ESPHOME_BIN"
  elif [ -x "$project_dir/.venv/bin/esphome" ]; then
    esphome="$project_dir/.venv/bin/esphome"
  else
    esphome="esphome"
  fi
  "$esphome" compile "$firmware_dir/pytaichenko.yaml"
  if [ ! -f "$factory_bin" ]; then
    echo "the compile did not produce a factory image at $factory_bin" >&2
    exit 1
  fi
fi

mkdir -p "$out_dir"
if [ "$manifest_only" -eq 0 ]; then
  cp "$factory_bin" "$out_dir/pytaichenko.factory.bin"
fi
cat > "$out_dir/manifest.json" <<JSON
{
  "name": "$name",
  "version": "$version",
  "new_install_prompt_erase": true,
  "new_install_improv_wait_time": 15,
  "builds": [
    {
      "chipFamily": "ESP32",
      "parts": [
        { "path": "pytaichenko.factory.bin", "offset": 0 }
      ]
    }
  ]
}
JSON
