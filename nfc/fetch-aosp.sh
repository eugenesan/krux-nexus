#!/usr/bin/env bash
# fetch-aosp.sh -- pull an AOSP source file out of android.googlesource.com
#
# GitHub tarballs and api.github.com are 404 from this machine, but
# android.googlesource.com is reachable, and gitiles returns base64 with
# ?format=TEXT. This does the fetch and the decode so nobody has to eyeball
# base64 again.
#
#   ./fetch-aosp.sh <commit> <path-within-repo> [destination]
#   ./fetch-aosp.sh tree <commit> <dir-path>          # directory listing, JSON
#
# Example:
#   ./fetch-aosp.sh master halimpl/bcm2079x/include/bcm2079x.h
set -euo pipefail
REPO="platform/external/libnfc-nci"
BASE="https://android.googlesource.com/$REPO/+/"

if [ "${1:-}" = "tree" ]; then
    mode=tree; ref="$2"; path="$3"
    curl -sS --max-time 60 "${BASE}${ref}/${path}/?format=JSON" | sed '1s/^)]}'"'"'//'
    exit 0
fi

ref="$1"; path="$2"; dest="${3:-$PWD/$(basename "$path")}"
mkdir -p "$(dirname "$dest")"
curl -sS --max-time 90 "${BASE}${ref}/${path}?format=TEXT" \
  | base64 -d > "$dest" 2>/dev/null \
  || { echo "fetch failed: ${ref}/${path}" >&2; exit 1; }
[ -s "$dest" ] || { echo "empty result: ${ref}/${path}" >&2; exit 1; }
printf '%s  (%s bytes)\n' "$dest" "$(stat -c %s "$dest")"
