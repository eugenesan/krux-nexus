#!/bin/bash
# Build MicroPython for the device: a static arm32 binary that runs
# inside TWRP (whose libc is bionic, so nothing dynamic may be linked).
#
# Buildroot cannot do this: BR2_PACKAGE_MICROPYTHON depends on
# !BR2_STATIC_LIBS, so buildroot's MicroPython is always dynamic.
#
# The unix port builds its host tools (mpy-cross, the qstr generator) with
# $(CC); for a cross build they have to be built with the host compiler
# first, or the port tries to *run* an arm32 mpy-cross.
#
# The armel sysroot (the headers and the static libc) is assembled by
# scripts/build-armel-sysroot.sh -- see docs/toolchain.md.
#
# The port's own files (the C modules and the unix-port patches) are
# committed in the MicroPython tree itself; there is nothing to copy
# on top of it any more.
#
# WHERE THE FILES LIVE
# --------------------
# All build output goes outside the checked-out `micropython` submodule.
# The source checkout remains the source of truth.
#
#   SRC=DIR        the MicroPython checkout to build (default:
#                  krux/firmware/micropython in this project)
#   MP_BUILD=DIR   where host and target build output goes (default:
#                  /tmp/opencode/mp-build)
#   CCACHE=CMD     compiler cache command (default: ccache)
#   SYSROOT=DIR    target sysroot (defaults to the host hard-float sysroot)
#   OUT=FILE       where the binary lands (default:
#                  rootfs/micropython-arm32)
#
# The sysroot is not carried around: it is rebuilt from the vendored
# packages in vendor/toolchain.ubuntu by scripts/build-armel-sysroot.sh.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC=${SRC:-$HERE/../../krux/firmware/micropython}
OUT=${OUT:-$HERE/../../rootfs/micropython-arm32}
MP_BUILD=${MP_BUILD:-/tmp/opencode/mp-build}
CC_CROSS=${CC_CROSS:-arm-linux-gnueabihf-}
CCACHE=${CCACHE:-ccache}
if [ -z "${SYSROOT:-}" ]; then
	if [ "$CC_CROSS" = arm-linux-gnueabihf- ] && [ -d /usr/arm-linux-gnueabihf ]; then
		SYSROOT=/usr/arm-linux-gnueabihf
	elif [ "$CC_CROSS" = arm-linux-gnueabi- ] && [ -d /usr/arm-linux-gnueabi ]; then
		SYSROOT=/usr/arm-linux-gnueabi
	else
		SYSROOT=/tmp/opencode/armel-sysroot
	fi
fi
HOST_BUILD=$MP_BUILD/mpy-cross
TARGET_BUILD=$MP_BUILD/target

[ -d "$SRC" ] || { echo "no micropython tree at $SRC (set SRC=)" >&2; exit 1; }
git -C "$SRC" rev-parse --git-dir >/dev/null 2>&1 || {
	echo "$SRC is not a git checkout" >&2; exit 1; }

# The armel sysroot: rebuilt from the vendored packages if it is not
# there (it is 71 MB of unpacked .deb, and it does not change).
[ -d "$SYSROOT" ] || sh "$HERE/../build-armel-sysroot.sh" "$SYSROOT"

# cUR is a mainline-MicroPython user C module.  Both its makefile and its
# source submodule live in our MicroPython fork (ports/unix/modules/cUR and
# lib/cUR).  Stage a copy of the source in the disposable build tree so the
# compiler's objects stay outside the source checkout.
CUR_SOURCE_DIR=$SRC/lib/cUR
CUR_PORT_MAKEFILE=$SRC/ports/unix/modules/cUR/micropython.mk
CUR_STAGE=$MP_BUILD/cUR
[ -e "$CUR_SOURCE_DIR/uUR.c" ] || {
	echo "cUR submodule is not initialized: run 'git submodule update --init --recursive lib/cUR' in the micropython submodule" >&2
	exit 1
}
[ -f "$CUR_PORT_MAKEFILE" ] || {
	echo "missing MicroPython cUR makefile: $CUR_PORT_MAKEFILE" >&2
	exit 1
}
rm -rf "$CUR_STAGE"
mkdir -p "$CUR_STAGE"
cp "$CUR_PORT_MAKEFILE" "$CUR_STAGE/"
cp -a "$CUR_SOURCE_DIR/uUR.c" "$CUR_SOURCE_DIR/src" "$CUR_STAGE/"

TREE="$SRC"
mkdir -p "$MP_BUILD"

# The unix port needs two of upstream's submodules: mbedtls (the TLS
# backend) and micropython-lib (the standard library, frozen into the
# binary).  They are pinned by the tag, so they are fetched at the pin.
git -C "$TREE" submodule update --init lib/mbedtls lib/micropython-lib

# 1. the host tools, with the host compiler
make -C "$TREE/mpy-cross" BUILD="$HOST_BUILD" \
	CC="$CCACHE gcc" HOSTCC="$CCACHE gcc" -j"$(nproc)"

# 2. the port itself, for the target
cd "$TREE/ports/unix"
# Cross-compiler flags are not tracked in object dependencies.  Clean only
# this out-of-tree target directory so switching ABI cannot reuse old objects.
make clean BUILD="$TARGET_BUILD"
make -j"$(nproc)" \
	BUILD="$TARGET_BUILD" \
	MICROPY_MPYCROSS="$HOST_BUILD/mpy-cross" \
	CC="$CCACHE ${CC_CROSS}gcc" HOSTCC="$CCACHE gcc" \
	CROSS_COMPILE=$CC_CROSS \
	MICROPY_PY_BTREE=0 \
	MICROPY_PY_USSL=0 \
	MICROPY_PY_FFI=0 \
	MICROPY_PY_CRYPTOLIB_CONSTS=1 \
	MICROPY_PY_CRYPTOLIB_CTR=1 \
	USER_C_MODULES="$CUR_STAGE" \
	# BBQR uses zlib-wrapped deflate.  The unix port's default ROM
	# level enables decompression only; without DEFLATE_COMPRESS the
	# encoder raises "Error compressing BBQR".
	CFLAGS_EXTRA="--sysroot=$SYSROOT -DMICROPY_PY_DEFLATE=1 -DMICROPY_PY_DEFLATE_COMPRESS=1" \
	LDFLAGS_EXTRA="-static --sysroot=$SYSROOT -lm"

file "$TARGET_BUILD/micropython"
if [[ "$CC_CROSS" == *gnueabihf* ]]; then
	if ! readelf -A "$TARGET_BUILD/micropython" | grep -q 'Tag_ABI_VFP_args: VFP registers'; then
		echo "target binary is not hard-float; check CC_CROSS and SYSROOT" >&2
		exit 1
	fi
fi
mkdir -p "$(dirname "$OUT")"
cp "$TARGET_BUILD/micropython" "$OUT"
rm -rf "$CUR_STAGE"
echo "  -> $OUT"
