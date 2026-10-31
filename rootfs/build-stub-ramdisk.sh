#!/bin/bash
# build-stub-ramdisk.sh -- build the stub ramdisk: TWRP's ramdisk, with
# our init stub in place of TWRP's recovery binary.
#
# WHAT THIS IS
# ------------
# The runtime design hides the firmware "under" TWRP: the device boots
# TWRP's ramdisk, but the `recovery` service is our own init stub
# (init-stub.c).  The stub decides at runtime whether to run TWRP (the
# user holds the volume up key) or to hand the display and the touch
# screen to the firmware.
#
# So the stub ramdisk is the stock TWRP ramdisk with three changes:
#
#   * sbin/recovery        -> sbin/recovery.stock   (TWRP, kept for the
#                                                     volume up key path)
#   * sbin/krux-init       <- our init stub        (the firmware path)
#   * init.recovery.service.rc: the service runs /sbin/krux-init
#
# The stock ramdisk comes out of the TWRP image in the project root
# (boot.img's ramdisk); the two patched files and the stub binary are
# in this directory.
#
# The result is stock/ramdisk-stub.  It is also packed into a boot image
# by make-boot-image.sh.
#
# The cpio is not byte-reproducible: it carries the filesystem's inode
# numbers and mtimes, so two packs of the same tree differ.  What is
# reproducible is the *content*, and that is what is checked.  The
# shipped ramdisk is what the device was verified with, so it is only
# replaced when --force is given.
#
# Usage:
#   ./build-stub-ramdisk.sh [--force] [--bundle] [TWRP.img]
#
# --bundle copies MicroPython, Krux and embit into /opt/krux in the
# ramdisk, so the boot image carries a read-only firmware instead of
# loading it from /data/krux.  The development deploy path still works:
# the init stub prefers /opt/krux when it is present.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
FORCE=""
BUNDLE=${BUNDLE:-0}
while [ $# -gt 0 ]; do
	case "$1" in
		--force)  FORCE=1; shift ;;
		--bundle) BUNDLE=1; shift ;;
		*)        break ;;
	esac
done
TWRP_IMG=${1:-$ROOT/artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img}
if [ "$BUNDLE" = "1" ]; then
	OUT=${OUT:-$HERE/stock/ramdisk-bundled}
else
	OUT=${OUT:-$HERE/stock/ramdisk-stub}
fi
WORK=${WORK:-/tmp/opencode/stub-ramdisk}
[ ! -d /usr/arm-linux-gnueabi ] || SYSROOT=/usr/arm-linux-gnueabi
SYSROOT=${SYSROOT:-/tmp/opencode/armel-sysroot}

[ -f "$TWRP_IMG" ] || { echo "no TWRP image at $TWRP_IMG" >&2; exit 1; }

# 1. the stock ramdisk, out of the TWRP boot image
rm -rf "$WORK"; mkdir -p "$WORK"
python3 "$HERE/bootimg.py" unpack "$TWRP_IMG" "$WORK/twrp" >/dev/null
cp "$WORK/twrp/ramdisk" "$WORK/ramdisk.gz"

# 2. unpack it
mkdir -p "$WORK/rd"
( cd "$WORK/rd" && gzip -dc ../ramdisk.gz | cpio -idm --quiet )

# 3. our stub in place of TWRP's recovery
[ -x "$HERE/stub/krux-init" ] || { echo "no stub/krux-init (build it first)" >&2; exit 1; }
mv "$WORK/rd/sbin/recovery" "$WORK/rd/sbin/recovery.stock"
cp "$HERE/stub/krux-init" "$WORK/rd/sbin/krux-init"
chmod 755 "$WORK/rd/sbin/krux-init" "$WORK/rd/sbin/recovery.stock"
cp "$HERE/stub/init.recovery.service.rc" "$WORK/rd/init.recovery.service.rc"

# 3b. the camera's firmware.
#
# The camera's CPP (the colour processing block) is programmed by the
# kernel, and the kernel loads its firmware through request_firmware(),
# which looks in /lib/firmware.  Under the ROM's Android that directory
# is the vendor's; in our ramdisk it is empty, so the CPP says
# "Invalid frame message" and no frame ever arrives.  The blob is the
# vendor's, and the version is not a free choice: the kernel checks the
# frame message against the firmware's stripe base, and the chromatrix
# the camera sends has 5 stripes and 266 words, which is
# 130 + 27*5 + 1 = 266 -- firmware 1.2.0.
CPP_FW="$ROOT/cam/fw/cpp_firmware_v1_2_0.fw"
if [ -f "$CPP_FW" ]; then
	mkdir -p "$WORK/rd/lib/firmware"
	cp "$CPP_FW" "$WORK/rd/lib/firmware/cpp_firmware_v1_2_0.fw"
	echo "  the camera's CPP firmware: $CPP_FW"
else
	echo "  !! no CPP firmware at $CPP_FW -- the camera cannot work" >&2
fi

# 3c. the read-only firmware, when asked for.
#
# The production boot image can carry the interpreter and the Python
# tree itself, so /data/krux is not needed for the firmware at all.
# The init stub checks /opt/krux first and falls back to /data/krux,
# which keeps the adb deploy path working during development.
if [ "$BUNDLE" = "1" ]; then
	BUNDLE_DIR="$WORK/rd/opt/krux"
	MICROPYTHON="$ROOT/rootfs/micropython-arm32"
	KRUX_SRC="$ROOT/krux/src"
	EMBIT_SRC="$ROOT/krux/vendor/embit/src/embit"

	[ -x "$MICROPYTHON" ] || {
		echo "  !! no MicroPython at $MICROPYTHON" >&2
		echo "     build it with scripts/build/build-micropython.sh" >&2
		exit 1
	}
	[ -f "$KRUX_SRC/boot.py" ] || {
		echo "  !! no Krux entry point at $KRUX_SRC/boot.py" >&2
		exit 1
	}
	[ -d "$EMBIT_SRC" ] || {
		echo "  !! no embit at $EMBIT_SRC" >&2
		exit 1
	}

	rm -rf "$BUNDLE_DIR"
	mkdir -p "$BUNDLE_DIR/krux" "$BUNDLE_DIR/flash" "$BUNDLE_DIR/embit"
	cp "$MICROPYTHON" "$BUNDLE_DIR/micropython"
	chmod 755 "$BUNDLE_DIR/micropython"
	CAMD_BIN="$ROOT/cam/camd"
	if [ -x "$CAMD_BIN" ]; then
		cp "$CAMD_BIN" "$BUNDLE_DIR/camd"
		chmod 755 "$BUNDLE_DIR/camd"
	else
		echo "  !! no camd at $CAMD_BIN -- the firmware has no camera" >&2
	fi
	CPP_FW_BUNDLE="$ROOT/cam/fw/cpp_firmware_v1_2_0.fw"
	if [ -f "$CPP_FW_BUNDLE" ]; then
		cp "$CPP_FW_BUNDLE" "$BUNDLE_DIR/cpp_firmware_v1_2_0.fw"
	fi
	cp "$KRUX_SRC"/*.py "$BUNDLE_DIR/"
	cp -R "$KRUX_SRC/krux/." "$BUNDLE_DIR/krux/"
	cp -R "$KRUX_SRC/flash/." "$BUNDLE_DIR/flash/"
	cp -R "$EMBIT_SRC/." "$BUNDLE_DIR/embit/"
	find "$BUNDLE_DIR" -name __pycache__ -type d -prune -exec rm -rf {} +
	echo "  the read-only firmware: $BUNDLE_DIR ($(du -sh "$BUNDLE_DIR" | cut -f1))"
fi

# 4. repack to a scratch file, and compare it with what ships
mkdir -p "$HERE/stock"
# -9: the bundled image carries the whole Python tree, and the boot
# partition is tight; the few extra seconds are worth the space.
( cd "$WORK/rd" && find . | cpio -o -H newc --quiet | gzip -9 ) > "$WORK/ramdisk-stub.new"
echo "  built $WORK/ramdisk-stub.new ($(stat -c %s "$WORK/ramdisk-stub.new") bytes)"

if [ -f "$OUT" ] && [ -z "$FORCE" ]; then
	rm -rf "$WORK/cmp-old" "$WORK/cmp-new"
	mkdir -p "$WORK/cmp-old" "$WORK/cmp-new"
	( cd "$WORK/cmp-old" && gzip -dc "$OUT"                    | cpio -idm --quiet 2>/dev/null )
	( cd "$WORK/cmp-new" && gzip -dc "$WORK/ramdisk-stub.new" | cpio -idm --quiet 2>/dev/null )
	# --no-dereference: the ramdisk is full of symlinks that point at
	# paths that do not exist off the device, and plain diff follows
	# them and reports every one as missing.
	if diff -rq --no-dereference "$WORK/cmp-old" "$WORK/cmp-new" >/dev/null 2>&1; then
		echo "  content: identical to the shipped ramdisk -- keeping the shipped bytes"
		exit 0
	fi
	echo "  content: DIFFERS from the shipped ramdisk"
	echo "           (the shipped one is kept; pass --force to replace it)"
	exit 1
fi

cp "$WORK/ramdisk-stub.new" "$OUT"
echo "  -> $OUT ($(stat -c %s "$OUT") bytes)"
