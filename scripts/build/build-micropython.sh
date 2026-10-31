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
# The build runs in the checked-out `micropython` submodule.  Its
# build-standard directory and object files are gitignored; the checked-in
# source remains the source of truth.
#
#   SRC=DIR        the MicroPython checkout to build (default:
#                  krux/firmware/micropython in this project)
#   SYSROOT=DIR    the armel sysroot (default: /tmp/opencode/armel-sysroot)
#   OUT=FILE       where the binary lands (default:
#                  rootfs/micropython-arm32)
#
# The sysroot is not carried around: it is rebuilt from the vendored
# packages in vendor/toolchain.ubuntu by scripts/build-armel-sysroot.sh.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC=${SRC:-$HERE/../../krux/firmware/micropython}
OUT=${OUT:-$HERE/../../rootfs/micropython-arm32}
[ ! -d /usr/arm-linux-gnueabi ] || SYSROOT=/usr/arm-linux-gnueabi
SYSROOT=${SYSROOT:-/tmp/opencode/armel-sysroot}
CC_CROSS=${CC_CROSS:-arm-linux-gnueabi-}

[ -d "$SRC" ] || { echo "no micropython tree at $SRC (set SRC=)" >&2; exit 1; }
git -C "$SRC" rev-parse --git-dir >/dev/null 2>&1 || {
	echo "$SRC is not a git checkout" >&2; exit 1; }

# The armel sysroot: rebuilt from the vendored packages if it is not
# there (it is 71 MB of unpacked .deb, and it does not change).
[ -d "$SYSROOT" ] || sh "$HERE/../build-armel-sysroot.sh" "$SYSROOT"

# cUR is a mainline-MicroPython user C module.  Both its makefile and its
# source submodule live in our MicroPython fork (ports/unix/modules/cUR and
# lib/cUR).  Stage a copy of the source next to the port makefile so the
# compiler's objects stay inside the disposable build tree.
CUR_SOURCE_DIR=$SRC/lib/cUR
CUR_PORT_MAKEFILE=$SRC/ports/unix/modules/cUR/micropython.mk
CUR_STAGE=$SRC/ports/unix/build-cUR
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

# Build in the checked-out MicroPython source.  A separate worktree was
# previously used here, but it made the build depend on a second checkout and
# complicated the cUR source paths.  build-standard is ignored by git.
TREE="$SRC"

# The unix port needs two of upstream's submodules: mbedtls (the TLS
# backend) and micropython-lib (the standard library, frozen into the
# binary).  They are pinned by the tag, so they are fetched at the pin.
git -C "$TREE" submodule update --init lib/mbedtls lib/micropython-lib

# 1. the host tools, with the host compiler
if [ ! -x "$TREE/mpy-cross/build/mpy-cross" ]; then
	make -C "$TREE/mpy-cross" clean >/dev/null 2>&1 || true
	make -C "$TREE/mpy-cross" CC=gcc HOSTCC=gcc -j"$(nproc)"
fi
mkdir -p "$TREE/ports/unix/build"
cp "$TREE/mpy-cross/build/mpy-cross" "$TREE/ports/unix/build/mpy-cross"

# 2. the port itself, for the target
cd "$TREE/ports/unix"
make clean >/dev/null 2>&1 || true
# Remove objects from an earlier host build as well as the regular build
# directory before the cross compile.
rm -rf build-standard vendor
make -j"$(nproc)" \
	CROSS_COMPILE=$CC_CROSS \
	HOSTCC=gcc \
	MICROPY_PY_BTREE=0 \
	MICROPY_PY_USSL=0 \
	MICROPY_PY_FFI=0 \
	MICROPY_PY_CRYPTOLIB_CONSTS=1 \
	MICROPY_PY_CRYPTOLIB_CTR=1 \
	USER_C_MODULES="$CUR_STAGE" \
	CFLAGS_EXTRA="--sysroot=$SYSROOT" \
	LDFLAGS_EXTRA="-static --sysroot=$SYSROOT -lm"

file build-standard/micropython
mkdir -p "$(dirname "$OUT")"
cp build-standard/micropython "$OUT"
rm -rf "$CUR_STAGE"
echo "  -> $OUT"
