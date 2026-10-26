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
[ ! -d /usr/arm-linux-gnueabi ] || SYSROOT=/usr/arm-linux-gnueabi
SYSROOT=${SYSROOT:-/tmp/opencode/armel-sysroot}
CC=${CC:-arm-linux-gnueabi-gcc}

[ -d "$SYSROOT" ] || { echo "no armel sysroot at $SYSROOT (see TOOLCHAIN.md)" >&2; exit 1; }

mkdir -p "$(dirname "$OUT")"
$CC -O2 -static -Wall \
    --sysroot="$SYSROOT" \
    -o "$OUT" \
    "$HERE/init-stub.c" -lpthread

file "$OUT"
echo "  -> $OUT"
