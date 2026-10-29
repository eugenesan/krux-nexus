#!/usr/bin/env bash
# Build camd with auto-exposure support.
# Usage: bash scripts/cam/build-ae-camd.sh [output_path]
set -e
OUT="${1:-cam/camd}"
cd "$(dirname "$0")/../.."

echo "=== Applying AE patches ==="
python3 tools/ae-patch.py --apply

echo "=== Building camd (hard-float) ==="
arm-linux-gnueabihf-gcc -O2 -o "$OUT" cam/camd.c \
    -lpthread -lrt -lm 2>&1 | tail -5

echo "=== Reverting AE patches (keep source clean) ==="
python3 tools/ae-patch.py --revert

echo "=== Built: $OUT ==="
ls -lh "$OUT"
EOF