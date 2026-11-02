#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ "${GITHUB_ACTIONS:-}" = true ]; then
  error="::error::"
else
  error="error: "
fi

if [ "$#" -ne 1 ]; then
  echo "usage: check-version.sh <tag>, for example v1.0.0" >&2
  exit 2
fi
tag="$1"
if ! [[ "$tag" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
  echo "${error}the release tag must look like v1.2.3, got '$tag'" >&2
  exit 1
fi
want="${BASH_REMATCH[1]}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
WEB_OUT_DIR="$work" bash "$root/firmware/build-web.sh" --manifest-only
firmware="$(sed -n 's/^  "version": "\(.*\)",$/\1/p' "$work/manifest.json")"

hub="$(awk '
  /^\[/ { section = $0; next }
  section == "[project]" && /^version[[:space:]]*=/ {
    sub(/^version[[:space:]]*=[[:space:]]*"/, "")
    sub(/".*$/, "")
    print
    exit
  }
' "$root/hub/pyproject.toml")"

site="$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)',\{0,1\}[[:space:]]*\$/\1/p" "$root/site/assets/config.js" | head -n 1)"

status=0
check() {
  if [ "$2" = "$want" ]; then
    echo "$1: $2"
  else
    echo "${error}$1 is ${2:-not set}, but the tag $tag needs $want" >&2
    status=1
  fi
}
check "firmware/pytaichenko.yaml (esphome: project: version)" "$firmware"
check "hub/pyproject.toml (project version)" "$hub"
check "site/assets/config.js (CONFIG.version)" "$site"

if [ "$status" -ne 0 ]; then
  echo "${error}set the version to $want in all three places, commit, and tag that commit" >&2
fi
exit "$status"
