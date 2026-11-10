#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ "${GITHUB_ACTIONS:-}" = true ]; then
  error="::error::"
else
  error="error: "
fi

if [ "$#" -ne 0 ]; then
  echo "usage: site-releases.sh, puts the newest release of each line on the site and writes site/assets/releases.js" >&2
  exit 2
fi

fail() {
  echo "${error}$1" >&2
  exit 1
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

gh release list --limit 1000 --json tagName,isDraft,isPrerelease > "$work/releases.json"
python3 -I -c '
import json
import re
import sys

newest = {}
with open(sys.argv[1]) as handle:
    releases = json.load(handle)
for release in releases:
    if release.get("isDraft") or release.get("isPrerelease"):
        continue
    match = re.fullmatch(r"v([12])\.([0-9]+)\.([0-9]+)", release.get("tagName") or "")
    if not match:
        continue
    key = (int(match.group(2)), int(match.group(3)))
    line = match.group(1)
    if line not in newest or key > newest[line][0]:
        newest[line] = (key, release["tagName"])
for line in sorted(newest):
    print(line, newest[line][1])
' "$work/releases.json" > "$work/newest.txt"
[ -s "$work/newest.txt" ] || fail "no published release of line 1 or 2 to put on the site"

rows=()
while read -r line tag <&3; do
  version="${tag#v}"
  git -C "$root" rev-parse -q --verify "refs/tags/$tag^{commit}" > /dev/null \
    || fail "the tag $tag is not in this checkout: check out with fetch-depth: 0"
  gh release download "$tag" --dir "$work/v$line" --pattern "pytaichenko-$version.factory.bin" --pattern "pytaichenko-$version-stl.zip"
  mkdir -p "$root/site/firmware/v$line" "$root/site/files/v$line"
  if [ "$line" = 1 ]; then
    if git -C "$root" cat-file -e "$tag:v1/case/stl" 2> /dev/null; then
      stl="v1/case/stl/"
    else
      stl="case/stl/"
    fi
    cp "$work/v1/pytaichenko-$version.factory.bin" "$root/site/firmware/v1/pytaichenko.factory.bin"
    bash "$root/v1/firmware/build-web.sh" --manifest-only --version "$version" --out "$root/site/firmware/v1"
  else
    stl="v2/case/stl/"
    bash "$root/v2/firmware/build-web.sh" --from-factory "$work/v2/pytaichenko-$version.factory.bin" --version "$version" --out "$root/site/firmware/v2"
  fi
  cp "$work/v$line/pytaichenko-$version-stl.zip" "$root/site/files/v$line/pytaichenko-stl.zip"
  rows+=("    '$line': { version: '$version', tag: '$tag', stlDir: '$stl' }")
  echo "version $line: $tag"
done 3< "$work/newest.txt"

{
  echo "window.PYTAICHENKO = window.PYTAICHENKO || {};"
  echo "window.PYTAICHENKO.RELEASES = {"
  last=$((${#rows[@]} - 1))
  for i in "${!rows[@]}"; do
    if [ "$i" -lt "$last" ]; then
      echo "${rows[$i]},"
    else
      echo "${rows[$i]}"
    fi
  done
  echo "};"
} > "$root/site/assets/releases.js"
