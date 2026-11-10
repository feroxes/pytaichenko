#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ "${GITHUB_ACTIONS:-}" = true ]; then
  error="::error::"
else
  error="error: "
fi

if [ "$#" -ne 1 ]; then
  echo "usage: check-version.sh <tag>, for example v1.0.0 or v2.0.0" >&2
  exit 2
fi
tag="$1"
if ! [[ "$tag" =~ ^v(([0-9]+)\.[0-9]+\.[0-9]+)$ ]]; then
  echo "${error}the release tag must look like v1.2.3, got '$tag'" >&2
  exit 1
fi
want="${BASH_REMATCH[1]}"
line="${BASH_REMATCH[2]}"

status=0
check() {
  if [ "$2" = "$want" ]; then
    echo "$1: $2"
  else
    echo "${error}$1 is ${2:-not set}, but the tag $tag needs $want" >&2
    status=1
  fi
}

case "$line" in
  1)
    work="$(mktemp -d)"
    trap 'rm -rf "$work"' EXIT
    bash "$root/v1/firmware/build-web.sh" --manifest-only --out "$work"
    firmware="$(sed -n 's/^  "version": "\(.*\)",$/\1/p' "$work/manifest.json")"
    hub="$(awk '
      /^\[/ { section = $0; next }
      section == "[project]" && /^version[[:space:]]*=/ {
        sub(/^version[[:space:]]*=[[:space:]]*"/, "")
        sub(/".*$/, "")
        print
        exit
      }
    ' "$root/v1/hub/pyproject.toml")"
    check "v1/firmware/pytaichenko.yaml (esphome: project: version)" "$firmware"
    check "v1/hub/pyproject.toml (project version)" "$hub"
    hint="set the version to $want in both places, commit, and tag that commit"
    ;;
  2)
    firmware=""
    if [ -f "$root/v2/firmware/VERSION" ]; then
      firmware="$(head -n 1 "$root/v2/firmware/VERSION" | tr -d '\r')"
    fi
    check "v2/firmware/VERSION (first line)" "$firmware"
    hint="set the first line of v2/firmware/VERSION to $want, commit, and tag that commit"
    ;;
  *)
    echo "${error}the tag $tag is for version line $line, but only lines 1 and 2 are released from this repository" >&2
    exit 1
    ;;
esac

if [ "$status" -ne 0 ]; then
  echo "${error}$hint" >&2
fi
exit "$status"
