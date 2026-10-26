#!/bin/bash
# Build MicroPython for the device by hand: a static arm32 binary that
# runs inside TWRP (whose libc is bionic, so nothing dynamic may be
# linked).  Buildroot cannot do this: BR2_PACKAGE_MICROPYTHON depends on
# !BR2_STATIC_LIBS.
#
# The unix port builds host tools (mpy-cross, the qstr generator) with
# $(CC); for a cross build they must use the host compiler, and the
# target needs the armel sysroot.
set -e

V=1.28.0
[ ! -d /usr/arm-linux-gnueabi ] || SYSROOT=/usr/arm-linux-gnueabi
SYSROOT=${SYSROOT:-/tmp/opencode/armel-sysroot}

cd /tmp/opencode
mkdir -p mp
cd mp

if [ ! -d micropython-$V ]; then
  [ -f micropython-$V.tar.xz ] || curl -sSLO https://micropython.org/resources/source/micropython-$V.tar.xz
  tar xf micropython-$V.tar.xz
fi
cd micropython-$V/ports/unix
make clean >/dev/null 2>&1 || true
make -j"$(nproc)" \
    CROSS_COMPILE=arm-linux-gnueabi- \
    HOSTCC=gcc \
    MICROPY_PY_BTREE=0 \
    MICROPY_PY_USSL=0 \
    MICROPY_PY_FFI=0 \
    CFLAGS_EXTRA="--sysroot=${SYSROOT}" \
    LDFLAGS_EXTRA="-static --sysroot=${SYSROOT} -lm" \
    2>&1 | tail -25
ls -la build/micropython 2>/dev/null
file build/micropython 2>/dev/null
