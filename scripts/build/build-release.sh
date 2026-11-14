#!/usr/bin/env bash
# Build the release image.  The default is the sealed image (no adb);
# KRUX_SEAL=0 builds the audit image, which is the same release with
# adb kept for the on-device inspection.
#
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export KRUX_RELEASE=1
export KRUX_SEAL=${KRUX_SEAL:-1}
NAME=release-audit
[ "$KRUX_SEAL" = "1" ] && NAME=release

# The kernel is the same air-gapped, wipe-capable one as the development
# image.  KERNEL_OUT persists across reboots (artifacts/ is on disk, not
# tmpfs); the first run builds it, later runs reuse it.
KERNEL_OUT="${KERNEL_OUT:-$ROOT/artifacts/built/ko}"
KERNEL_IMG="$KERNEL_OUT/arch/arm/boot/zImage-dtb"
if [ ! -f "$KERNEL_IMG" ]; then
    echo "kernel: $KERNEL_IMG is missing; building it (about 15 minutes)"
    bash tools/build-kernel.sh --tree linux --out "$KERNEL_OUT"
fi

OUT="$ROOT/rootfs/micropython-arm32-release" bash scripts/build/build-micropython.sh
KRUX_RELEASE=1 bash scripts/cam/build-camd.sh cam/camd-release
MICROPYTHON="$ROOT/rootfs/micropython-arm32-release" CAMD="$ROOT/cam/camd-release" \
    bash rootfs/build-stub-ramdisk.sh --standard --bundle --force
RAMDISK=artifacts/built/ramdisk-bundled bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    "$KERNEL_IMG" \
    "artifacts/built/boot-standard-${NAME}.img"
bash scripts/security-check.sh "artifacts/built/boot-standard-${NAME}.img"
