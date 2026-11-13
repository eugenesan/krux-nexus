#!/usr/bin/env bash
# Build the release image.  The default is the sealed image (no adb);
# KRUX_SEAL=0 builds the audit image, which is the same release with
# adb kept for the on-device inspection.
#
# The kernel fragment and the release cmdline are Phase 3 of the plan;
# until then this pairs the release ramdisk with the current kernel.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export KRUX_RELEASE=1
export KRUX_SEAL=${KRUX_SEAL:-1}
NAME=release-audit
[ "$KRUX_SEAL" = "1" ] && NAME=release
OUT="$ROOT/rootfs/micropython-arm32-release" bash scripts/build/build-micropython.sh
KRUX_RELEASE=1 bash scripts/cam/build-camd.sh cam/camd-release
MICROPYTHON="$ROOT/rootfs/micropython-arm32-release" CAMD="$ROOT/cam/camd-release" \
    bash rootfs/build-stub-ramdisk.sh --standard --bundle --force
RAMDISK=artifacts/built/ramdisk-bundled bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    "artifacts/built/boot-standard-${NAME}.img"
bash scripts/security-check.sh "artifacts/built/boot-standard-${NAME}.img"
