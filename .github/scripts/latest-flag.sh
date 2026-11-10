#!/usr/bin/env bash
set -euo pipefail

if [ "${GITHUB_ACTIONS:-}" = true ]; then
  error="::error::"
else
  error="error: "
fi

if [ "$#" -ne 1 ]; then
  echo "usage: latest-flag.sh <tag>, prints true or false for gh release create --latest" >&2
  exit 2
fi
tag="$1"
if ! [[ "$tag" =~ ^v([0-9]+)\.[0-9]+\.[0-9]+$ ]]; then
  echo "${error}the release tag must look like v1.2.3, got '$tag'" >&2
  exit 1
fi

line="${BASH_REMATCH[1]}"
case "$line" in
  1 | 2) ;;
  *)
    echo "${error}the tag $tag is for version line $line, but only lines 1 and 2 are released from this repository" >&2
    exit 1
    ;;
esac

releases="$(gh release list --limit 1000 --json tagName,isDraft,isPrerelease)"
printf '%s' "$releases" | python3 -I -c '
import json
import re
import sys

line = int(sys.argv[1])
own = tuple(int(part) for part in sys.argv[2].split("."))
published = []
for release in json.load(sys.stdin):
    if release.get("isDraft") or release.get("isPrerelease"):
        continue
    match = re.fullmatch(r"v([0-9]+)\.([0-9]+)\.([0-9]+)", release.get("tagName") or "")
    if match:
        published.append(tuple(int(part) for part in match.groups()))
higher_line = any(major in (1, 2) and major > line for major, _, _ in published)
newer_same = any(release[0] == line and release > own for release in published)
print("false" if higher_line or newer_same else "true")
' "$line" "${tag#v}"
