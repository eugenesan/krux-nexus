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
# The result is artifacts/built/ramdisk-stub.  It is also packed into a
# boot image by make-boot-image.sh.
#
# The cpio is not byte-reproducible: it carries the filesystem's inode
# numbers and mtimes, so two packs of the same tree differ.  What is
# reproducible is the *content*, and that is what is checked.  The
# shipped ramdisk is what the device was verified with, so it is only
# replaced when --force is given.
#
# Usage:
#   ./build-stub-ramdisk.sh [--force] [--bundle] [--standard] [TWRP.img]
#
# --bundle copies MicroPython, Krux and embit into /opt/krux in the
# ramdisk, so the boot image carries a read-only firmware instead of
# loading it from /data/krux.  It is for the standard delivery mode:
# during development the firmware is deployed to /data/krux, and the
# development stub runs that copy, so a push always takes effect.
#
# --standard builds the init stub for the standard delivery mode: the
# firmware is the read-only copy in the ramdisk, it does not start by
# itself, and the device comes up in TWRP.  The firmware is then
# started with the volume up key during the boot, or with
# /sbin/start-krux from adb or from TWRP's terminal.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
FORCE=""
BUNDLE=${BUNDLE:-0}
# The standard delivery mode: the firmware is not started at boot.
STANDARD=${STANDARD:-0}
# gzip (default TWRP), xz (best size; needs CONFIG_RD_XZ), lzma or lz4.
#
# Two different unpackers are involved, and that is the whole subtlety:
#
#   * `fastboot boot <image>` -- the KERNEL unpacks the initramfs.  Our
#     kernel (lineageos_hammerhead_defconfig) has CONFIG_RD_XZ=y, so an
#     XZ ramdisk boots and works on this path.  The earlier capture
#     kernels (cam-yonly etc.) did NOT have RD_XZ, which is why commit
#     d060148 recorded "an XZ ramdisk is accepted but boots nothing".
#   * An image FLASHED to the boot partition -- the BOOTLOADER (aboot)
#     unpacks it, and this bootloader only looks for a gzip ramdisk.
#     An XZ ramdisk there boots nothing, silently.
#
# This project never flashes (fastboot boot only), so XZ is usable here.
# gzip stays the default so the same image is also safe to flash; opt in
# with COMPRESS=xz when you want the ~40% smaller ramdisk on the
# fastboot path.
COMPRESS=${COMPRESS:-gzip}
while [ $# -gt 0 ]; do
	case "$1" in
		--force)  FORCE=1; shift ;;
		--bundle) BUNDLE=1; shift ;;
		--standard) STANDARD=1; shift ;;
		*)        break ;;
	esac
done
TWRP_IMG=${1:-$ROOT/artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img}
if [ "$BUNDLE" = "1" ]; then
	OUT=${OUT:-$ROOT/artifacts/built/ramdisk-bundled}
else
	OUT=${OUT:-$ROOT/artifacts/built/ramdisk-stub}
fi
mkdir -p "$(dirname "$OUT")"
WORK=${WORK:-/tmp/opencode/stub-ramdisk}

[ -f "$TWRP_IMG" ] || { echo "no TWRP image at $TWRP_IMG" >&2; exit 1; }

# 1. the stock ramdisk, out of the TWRP boot image
rm -rf "$WORK"; mkdir -p "$WORK"
python3 "$HERE/bootimg.py" unpack "$TWRP_IMG" "$WORK/twrp" >/dev/null
cp "$WORK/twrp/ramdisk" "$WORK/ramdisk.gz"

# 2. unpack it
mkdir -p "$WORK/rd"
( cd "$WORK/rd" && gzip -dc ../ramdisk.gz | cpio -idm --quiet )

# 3. our stub in place of TWRP's recovery
# The stub is built here, so that the mode of the image and the mode of
# the stub cannot drift apart.
STUB_FLAGS=""
[ "$STANDARD" = "1" ] && STUB_FLAGS="--standard"
bash "$HERE/build-stub-init.sh" $STUB_FLAGS "$HERE/stub/krux-init" >/dev/null
# Prune what the stub never uses.  TWRP ships terminfo (6.6 MB) and a
# Python 3.10 tree (3.2 MB); the stub runs neither, and the boot
# partition is tight.
rm -rf "$WORK/rd/sbin/etc/terminfo" "$WORK/rd/etc/python"

# TWRP's FBE keystore service and the UBSan runtime are not used by the
# stub path.  No binary in the ramdisk has a DT_NEEDED on the UBSan
# library.  KEEP_KEYSTORE=1 keeps the keystore binaries for TWRP FBE.
if [ "${KEEP_KEYSTORE:-0}" != "1" ]; then
	rm -f "$WORK/rd/sbin/keystore" "$WORK/rd/sbin/keystore_auth" \
	      "$WORK/rd/sbin/keystore_cli"
fi
rm -f "$WORK/rd/sbin/libclang_rt.ubsan_standalone-arm-android.so"

# TWRP ships adbd, the linker, libc and libdexfile unstripped.  Strip
# the symbol tables (the dynamic symbols stay); this saves ~0.9 MB
# uncompressed without changing what runs.
STRIP=${STRIP:-arm-linux-gnueabihf-strip}
if command -v "$STRIP" >/dev/null 2>&1; then
	for f in adbd linker libc.so libdexfile.so; do
		[ -f "$WORK/rd/sbin/$f" ] && "$STRIP" "$WORK/rd/sbin/$f"
	done
fi

mv "$WORK/rd/sbin/recovery" "$WORK/rd/sbin/recovery.stock"
cp "$HERE/stub/krux-init" "$WORK/rd/sbin/krux-init"
chmod 755 "$WORK/rd/sbin/krux-init" "$WORK/rd/sbin/recovery.stock"
# The way the standard image is started without a reboot: from adb or
# from TWRP's own terminal.
cat > "$WORK/rd/sbin/start-krux" <<'SCRIPT'
#!/bin/sh
# start-krux -- start the firmware on a running standard image.
#
# The standard image comes up with TWRP: the stub has become the
# recovery service, and the firmware waits to be asked.  This script
# is the ask.  It works from adb and from TWRP's own terminal.
#
# The work is handed to a new session first: stopping the "krux"
# service ends TWRP, and with it the shell that runs this script when
# that shell is TWRP's terminal.  A new session is not in the service's
# process group, so it survives.
setsid /sbin/krux-init start >/dev/null 2>&1 &
echo "the firmware is starting"
SCRIPT
chmod 755 "$WORK/rd/sbin/start-krux"
# adb on demand: the sealed image keeps it off, this brings it up.
cp "$HERE/stub/enable-adb" "$WORK/rd/sbin/enable-adb"
chmod 755 "$WORK/rd/sbin/enable-adb"
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
# The init stub prefers /data/krux and falls back to /opt/krux, so the
# adb deploy path keeps working during development: a bundled image
# without a development tree on the data partition still boots.
if [ "$BUNDLE" = "1" ]; then
	BUNDLE_DIR="$WORK/rd/opt/krux"
	MICROPYTHON=${MICROPYTHON:-$ROOT/rootfs/micropython-arm32}
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
	CAMD_BIN=${CAMD:-$ROOT/cam/camd}
	if [ -x "$CAMD_BIN" ]; then
		cp "$CAMD_BIN" "$BUNDLE_DIR/camd"
		chmod 755 "$BUNDLE_DIR/camd"
	else
		echo "  !! no camd at $CAMD_BIN -- the firmware has no camera" >&2
	fi
	cp "$KRUX_SRC"/*.py "$BUNDLE_DIR/"
	cp -R "$KRUX_SRC/krux/." "$BUNDLE_DIR/krux/"
	cp -R "$KRUX_SRC/flash/." "$BUNDLE_DIR/flash/"
	cp -R "$EMBIT_SRC/." "$BUNDLE_DIR/embit/"
	find "$BUNDLE_DIR" -name __pycache__ -type d -prune -exec rm -rf {} +
	echo "  the read-only firmware: $BUNDLE_DIR ($(du -sh "$BUNDLE_DIR" | cut -f1))"
fi

# 4. repack to a scratch file, and compare it with what ships
# -9: the bundled image carries the whole Python tree, and the boot
# partition is tight; the few extra seconds are worth the space.
( cd "$WORK/rd" && find . | cpio -o -H newc --quiet ) > "$WORK/ramdisk.cpio"
case "$COMPRESS" in
	gzip) gzip -9 -c "$WORK/ramdisk.cpio" > "$WORK/ramdisk-stub.new" ;;
	xz)   xz -9 --check=crc32 -c "$WORK/ramdisk.cpio" > "$WORK/ramdisk-stub.new" ;;
	lzma) lzma -9 -c "$WORK/ramdisk.cpio" > "$WORK/ramdisk-stub.new" ;;
	lz4)  lz4 -9 -c "$WORK/ramdisk.cpio" > "$WORK/ramdisk-stub.new" ;;
	*) echo "unknown COMPRESS=$COMPRESS (gzip|xz|lzma|lz4)" >&2; exit 1 ;;
esac
rm -f "$WORK/ramdisk.cpio"
echo "  built $WORK/ramdisk-stub.new ($(stat -c %s "$WORK/ramdisk-stub.new") bytes, $COMPRESS)"

if [ -f "$OUT" ] && [ -z "$FORCE" ]; then
	rm -rf "$WORK/cmp-old" "$WORK/cmp-new"
	mkdir -p "$WORK/cmp-old" "$WORK/cmp-new"
	decompress() {
		case "$(file -b "$1")" in
			*gzip*) gzip -dc "$1" ;;
			*XZ*)   xz -dc "$1" ;;
			*LZMA*) lzma -dc "$1" ;;
			*LZ4*)  lz4 -dc "$1" ;;
			*)      cat "$1" ;;
		esac
	}
	( cd "$WORK/cmp-old" && decompress "$OUT"                    | cpio -idm --quiet 2>/dev/null )
	( cd "$WORK/cmp-new" && decompress "$WORK/ramdisk-stub.new" | cpio -idm --quiet 2>/dev/null )
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
