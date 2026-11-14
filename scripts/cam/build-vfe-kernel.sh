#!/usr/bin/env bash
# Build an instrumented camera kernel for VFE overflow diagnosis.
# Usage: bash scripts/cam/build-vfe-kernel.sh [output_dir]
#
# The instrumentation patch and the devtmpfs toggle are applied to the
# kernel build only, never to tracked files:
#   * tools/vfe-instrument.py --apply / --revert
#   * a temporary keeplist with CONFIG_DEVTMPFS=y, passed via KEEPLISTS
set -e
OUTDIR="${1:-/tmp/opencode/ko-vfe}"
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"

KEEP_TMP="$(mktemp)"
cleanup() {
    python3 tools/vfe-instrument.py --revert --linux-dir linux >/dev/null 2>&1 || true
    rm -f "$KEEP_TMP"
}
trap cleanup EXIT

echo "=== Applying VFE instrumentation ==="
python3 tools/vfe-instrument.py --apply --linux-dir linux

echo "=== Enabling devtmpfs for this build only ==="
sed 's/^CONFIG_DEVTMPFS=n/CONFIG_DEVTMPFS=y/' \
    tools/profiles/hammerhead-keep.list > "$KEEP_TMP"

echo "=== Building kernel ==="
KEEPLISTS="$KEEP_TMP $ROOT/tools/profiles/hammerhead-camera.list" \
    TOOLCHAIN=host bash tools/build-kernel.sh --out "$OUTDIR"

echo "=== Kernel built at $OUTDIR ==="
echo "zImage: $OUTDIR/arch/arm/boot/zImage-dtb"
