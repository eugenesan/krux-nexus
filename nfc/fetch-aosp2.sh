#!/usr/bin/env bash
# fetch-aosp2.sh -- like fetch-aosp.sh, but for a named repo.
#   ./fetch-aosp2.sh <repo> tree <ref> <path>
#   ./fetch-aosp2.sh <repo> get <ref> <path> [dest]
set -euo pipefail
REPO="$1"; MODE="$2"; REF="$3"; PATH_IN="$4"
BASE="https://android.googlesource.com/${REPO}/+/"
if [ "$MODE" = tree ]; then
    curl -sS --max-time 60 "${BASE}${REF}/${PATH_IN}?format=JSON" \
      | sed '1s/^)]}.//'
    exit 0
fi
dest="${5:-$PWD/$(basename "$PATH_IN")}"
mkdir -p "$(dirname "$dest")"
curl -sS --max-time 90 "${BASE}${REF}/${PATH_IN}?format=TEXT" | base64 -d > "$dest" 2>/dev/null \
  || { echo "fetch failed: ${REPO}/${REF}/${PATH_IN}" >&2; exit 1; }
[ -s "$dest" ] || { echo "empty: ${REPO}/${REF}/${PATH_IN}" >&2; exit 1; }
printf '%s  (%s bytes)\n' "$dest" "$(stat -c %s "$dest")"
