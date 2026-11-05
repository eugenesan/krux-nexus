#!/bin/bash
# build-kexec.sh -- build a static ARM kexec for the hammerhead stub.
#
# The wipe path uses kexec-tools' ARM zImage loader, which understands
# Android boot images directly (kernel + ramdisk + cmdline).  The binary
# must be static: the stub ramdisk has bionic, not the armel sysroot.
#
#   SRC=DIR   kexec-tools checkout (default: /tmp/opencode/kexec-tools)
#   OUT=FILE  output binary (default: artifacts/built/kexec-arm)
#
# The checkout is cloned on first use.  The build needs autoconf,
# automake, make and the arm-linux-gnueabihf cross compiler.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC=${SRC:-/tmp/opencode/kexec-tools}
OUT=${OUT:-$ROOT/artifacts/built/kexec-arm}
CROSS=${CROSS:-arm-linux-gnueabihf-}
JOBS=${JOBS:-$(nproc)}

if [ ! -d "$SRC/.git" ]; then
	git clone --depth 1 https://github.com/horms/kexec-tools.git "$SRC"
fi

cd "$SRC"
[ -x configure ] || ./bootstrap
make distclean >/dev/null 2>&1 || true
./configure --host="${CROSS%?}" --target="${CROSS%?}" \
	--without-xen --without-zlib \
	CFLAGS="-O2 -static" LDFLAGS="-static" >/dev/null
make -j"$JOBS" >/dev/null
"${CROSS}strip" build/sbin/kexec
mkdir -p "$(dirname "$OUT")"
cp build/sbin/kexec "$OUT"
chmod 755 "$OUT"
echo "  -> $OUT ($(stat -c %s "$OUT") bytes)"
