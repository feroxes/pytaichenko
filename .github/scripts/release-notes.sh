#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
changelog="$root/CHANGELOG.md"

if [ "${GITHUB_ACTIONS:-}" = true ]; then
  error="::error::"
else
  error="error: "
fi

if [ "$#" -ne 1 ] || ! [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "usage: release-notes.sh <version>, for example 1.0.0" >&2
  exit 2
fi
version="$1"

if [ ! -f "$changelog" ]; then
  echo "${error}CHANGELOG.md is missing" >&2
  exit 1
fi

notes="$(awk -v heading="## [$version]" '
  !found && ($0 == heading || index($0, heading " ") == 1) { found = 1; next }
  found && /^## / { exit }
  found && /^\[[^]]+\]: / { exit }
  found { print }
' "$changelog" | sed '/[^[:space:]]/,$!d')"

if [ -z "$notes" ]; then
  echo "${error}CHANGELOG.md has no '## [$version] - <date>' section with notes; add it before tagging v$version" >&2
  exit 1
fi
printf '%s\n' "$notes"
