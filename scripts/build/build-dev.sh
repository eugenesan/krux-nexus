#!/usr/bin/env bash
# Build the development image: logs, dumps, adb, the framebuffer dump.
# The release profile is the default for the low-level builders; this
# wrapper is the explicit dev opt-out.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export KRUX_RELEASE=0
export KRUX_SEAL=0

# The kernel image is a build artifact, not a /tmp cache: /tmp is a
# tmpfs here and is emptied by a reboot.  Keep the build in artifacts/
# so a later run does not have to rebuild the kernel from scratch, and
# build it on the first run.
KERNEL_OUT="${KERNEL_OUT:-$ROOT/artifacts/built/ko}"
KERNEL_IMG="$KERNEL_OUT/arch/arm/boot/zImage-dtb"
if [ ! -f "$KERNEL_IMG" ]; then
    echo "kernel: $KERNEL_IMG is missing; building it (about 15 minutes)"
    bash tools/build-kernel.sh --tree linux --out "$KERNEL_OUT"
fi

bash scripts/build/build-micropython.sh
bash scripts/cam/build-camd.sh cam/camd
bash rootfs/build-stub-ramdisk.sh --force
RAMDISK=artifacts/built/ramdisk-stub bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    "$KERNEL_IMG" \
    artifacts/built/boot-dev.img
