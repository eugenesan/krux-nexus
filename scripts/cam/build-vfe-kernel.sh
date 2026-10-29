#!/usr/bin/env bash
# Build an instrumented camera kernel for VFE overflow diagnosis.
# Usage: bash scripts/cam/build-vfe-kernel.sh [output_dir]
set -e
OUTDIR="${1:-/tmp/opencode/ko-vfe}"
cd "$(dirname "$0")/../.."

echo "=== Applying VFE instrumentation ==="
python3 tools/vfe-instrument.py --apply --linux-dir linux

echo "=== Enabling devtmpfs ==="
sed -i 's/^CONFIG_DEVTMPFS=n/CONFIG_DEVTMPFS=y/' tools/profiles/hammerhead-keep.list

echo "=== Building kernel ==="
TOOLCHAIN=host bash tools/build-kernel.sh --out "$OUTDIR"

echo "=== Restoring devtmpfs ==="
sed -i 's/^CONFIG_DEVTMPFS=y/CONFIG_DEVTMPFS=n/' tools/profiles/hammerhead-keep.list

echo "=== Reverting instrumentation (keep source clean) ==="
python3 tools/vfe-instrument.py --revert --linux-dir linux

echo "=== Kernel built at $OUTDIR ==="
echo "zImage: $OUTDIR/arch/arm/boot/zImage-dtb"
EOF
chmod +x scripts/cam/build-vfe-kernel.sh
echo "created"