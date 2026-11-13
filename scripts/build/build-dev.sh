#!/usr/bin/env bash
# Build the development image: logs, dumps, adb, the framebuffer dump.
# The release profile is the default for the low-level builders; this
# wrapper is the explicit dev opt-out.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export KRUX_RELEASE=0
export KRUX_SEAL=0
bash scripts/build/build-micropython.sh
bash scripts/cam/build-camd.sh cam/camd
bash rootfs/build-stub-ramdisk.sh --force
RAMDISK=artifacts/built/ramdisk-stub bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-dev.img
