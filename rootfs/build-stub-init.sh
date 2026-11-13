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
#   ./build-stub-init.sh [--standard] [output]
#
# --standard builds the stub for the standard delivery mode: the
# firmware is not started at boot, and the device comes up with TWRP
# instead.  The data partition can override it at run time (see
# init-stub.c).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
STANDARD=0
while [ $# -gt 0 ]; do
	case "$1" in
		--standard) STANDARD=1; shift ;;
		*) break ;;
	esac
done
OUT=${1:-$HERE/stub/krux-init}
# The host's hard-float sysroot.  The Debian armel (soft-float)
# sysroot and the Linaro / ARM GNU toolchains are obsolete; see
# docs/toolchain.md.
SYSROOT=${SYSROOT:-/usr/arm-linux-gnueabihf}
CC=${CC:-arm-linux-gnueabihf-gcc}

[ -d "$SYSROOT" ] || { echo "no sysroot at $SYSROOT (see TOOLCHAIN.md)" >&2; exit 1; }

mkdir -p "$(dirname "$OUT")"
DEFS=""
[ "$STANDARD" = "1" ] && DEFS="-DDEFAULT_STANDARD=1"
# The release profile is the default; the development wrappers set
# KRUX_RELEASE=0 (and KRUX_SEAL=0).  The seal removes adb.
[ "${KRUX_RELEASE:-1}" = "1" ] && DEFS="$DEFS -DKRUX_RELEASE"
[ "${KRUX_SEAL:-1}" = "1" ] && DEFS="$DEFS -DKRUX_SEAL"

$CC -Os -static -s -Wall \
    -march=armv7-a -mfpu=neon -mfloat-abi=hard \
    --sysroot="$SYSROOT" \
    $DEFS \
    -o "$OUT" \
    "$HERE/init-stub.c" -lpthread

file "$OUT"
echo "  -> $OUT"
