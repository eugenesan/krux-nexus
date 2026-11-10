#!/bin/bash
# build-stub-init.sh -- build the init stub: the binary that stands in
# for TWRP's own init as the `recovery` service.
#
# The stub is built static (the ramdisk has no libc of its own that is
# worth relying on) with the armel toolchain -- the same one the
# firmware's MicroPython is built with.  See TOOLCHAIN.md.
#
# The result is stub/krux-init, which build-stub-ramdisk.sh installs as
# /sbin/krux-init in the stub ramdisk.
#
# Usage:
#   ./build-stub-init.sh [output]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=${1:-$HERE/stub/krux-init}
# The host's hard-float sysroot.  The Debian armel (soft-float)
# sysroot and the Linaro / ARM GNU toolchains are obsolete; see
# docs/toolchain.md.
SYSROOT=${SYSROOT:-/usr/arm-linux-gnueabihf}
CC=${CC:-arm-linux-gnueabihf-gcc}

[ -d "$SYSROOT" ] || { echo "no sysroot at $SYSROOT (see TOOLCHAIN.md)" >&2; exit 1; }

mkdir -p "$(dirname "$OUT")"
$CC -Os -static -s -Wall \
    -march=armv7-a -mfpu=neon -mfloat-abi=hard \
    --sysroot="$SYSROOT" \
    -o "$OUT" \
    "$HERE/init-stub.c" -lpthread

file "$OUT"
echo "  -> $OUT"
