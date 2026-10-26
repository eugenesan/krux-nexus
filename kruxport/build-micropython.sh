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
# The armel sysroot (the headers and the static libc) is built by
# tools/fetch-toolchain.sh -- see the project's TOOLCHAIN.md.
#
# The port's own files (the I2C class and the machine-module glue) are
# applied on top of the upstream tree by apply-port.sh.
#
# WHERE THE FILES LIVE
# --------------------
# The upstream tree is NOT copied around: it is a git worktree of
# standalone/vendor/micropython at the tag we build, and it lives in the
# project.  Only the *build output* (the object files, and the tree's own
# build/ directory) is disposable, so it goes to a BUILD= directory that
# defaults to tmpfs.
#
# This is the same split the kernel build uses: the source tree is in the
# project, the build output goes to /tmp/opencode/ko-cap.  A build tree
# is a few gigabytes of intermediate objects that nobody wants in a
# backup, and it is rebuilt many times a day; the source tree is edited
# by hand and must survive.
#
#   BUILD=DIR      where the build tree and its objects go (default: a
#                  tmpfs directory).  Safe to delete at any time; the
#                  next build recreates it.
#   SRC=DIR        the upstream micropython checkout to build (default:
#                  standalone/vendor/micropython in this project)
#   SYSROOT=DIR    the armel sysroot (default: /tmp/opencode/armel-sysroot)
#
# The sysroot is not carried around: it is rebuilt from the vendored
# packages in standalone/vendor/toolchain.ubuntu by
# tools/build-armel-sysroot.sh.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
V=1.28.0
TAG=${TAG:-v$V}
SRC=${SRC:-$HERE/../vendor/micropython}
BUILD=${BUILD:-/tmp/opencode/mp-build}
[ ! -d /usr/arm-linux-gnueabi ] || SYSROOT=/usr/arm-linux-gnueabi
SYSROOT=${SYSROOT:-/tmp/opencode/armel-sysroot}
CC_CROSS=${CC_CROSS:-arm-linux-gnueabi-}

[ -d "$SRC" ] || { echo "no micropython tree at $SRC (set SRC=)" >&2; exit 1; }
git -C "$SRC" rev-parse --git-dir >/dev/null 2>&1 || {
	echo "$SRC is not a git checkout" >&2; exit 1; }

# The armel sysroot: rebuilt from the vendored packages if it is not
# there (it is 71 MB of unpacked .deb, and it does not change).
[ -d "$SYSROOT" ] || sh "$HERE/../tools/build-armel-sysroot.sh" "$SYSROOT"

# The tree we build: a worktree of the vendored checkout at $TAG.  It is
# disposable -- the tag in the project is the source of truth.
TREE="$BUILD/micropython-$V"
mkdir -p "$BUILD"
if [ ! -e "$TREE" ]; then
	# a worktree that was deleted from under us leaves its registration
	# behind, and git then refuses to re-create it
	git -C "$SRC" worktree prune
	git -C "$SRC" worktree add --detach "$TREE" "$TAG"
fi

# The unix port needs two of upstream's submodules: mbedtls (the TLS
# backend) and micropython-lib (the standard library, frozen into the
# binary).  They are pinned by the tag, so they are fetched at the pin.
git -C "$TREE" submodule update --init lib/mbedtls lib/micropython-lib

# 0. the port's own files
sh "$HERE/apply-port.sh" "$TREE"

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
make -j"$(nproc)" \
	CROSS_COMPILE=$CC_CROSS \
	HOSTCC=gcc \
	MICROPY_PY_BTREE=0 \
	MICROPY_PY_USSL=0 \
	MICROPY_PY_FFI=0 \
	MICROPY_PY_CRYPTOLIB_CONSTS=1 \
	MICROPY_PY_CRYPTOLIB_CTR=1 \
	CFLAGS_EXTRA="--sysroot=$SYSROOT" \
	LDFLAGS_EXTRA="-static --sysroot=$SYSROOT -lm"

file build-standard/micropython
echo "  -> $HERE/micropython-arm32"
cp build-standard/micropython "$HERE/micropython-arm32"
