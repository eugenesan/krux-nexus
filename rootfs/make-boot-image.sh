#!/bin/bash
# make-boot-image.sh -- pack the stub boot image: the kernel plus the
# stub ramdisk, in the format the stock bootloader expects.
#
# This is the image the runtime design boots: the device starts TWRP
# (so the mounts, the USB gadget and adb come up as they always did),
# but the `recovery` service is our init stub, which hands the display
# and the touch screen to the firmware.
#
# The header (the load addresses, the page size, the command line) is
# NOT invented here: it is taken from an existing, working boot image
# with bootimg.py unpack.  The stock bootloader loads the kernel and
# the ramdisk at exactly the addresses in the header, so getting them
# wrong produces a device that boots to nothing at all, with no error
# message.  That is why the template is a real image and not a set of
# constants.
#
# Usage:
#   ./make-boot-image.sh [TEMPLATE.img] [KERNEL] [OUT.img]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
TEMPLATE=${1:-$ROOT/rootfs/stock/boot-ourkern-clean-twrp.img}
# The kernel the shipped boot-stub.img pairs with.  It is NOT the same
# one the current kernel build produces: this is the kernel that was
# flashed and verified with the stub, so it is kept next to the image
# rather than rebuilt.
KERNEL=${2:-$ROOT/rootfs/stock/kernel-stub}
OUT=${3:-$ROOT/rootfs/stock/boot-stub.img}
RAMDISK=$HERE/stock/ramdisk-stub
WORK=${WORK:-/tmp/opencode/boot-stub}

[ -f "$TEMPLATE" ] || { echo "no template boot image at $TEMPLATE" >&2; exit 1; }
[ -f "$KERNEL" ]    || { echo "no kernel at $KERNEL" >&2; exit 1; }
[ -f "$RAMDISK" ]   || { echo "no stub ramdisk at $RAMDISK (run build-stub-ramdisk.sh)" >&2; exit 1; }

rm -rf "$WORK"; mkdir -p "$WORK"
python3 "$HERE/bootimg.py" unpack "$TEMPLATE" "$WORK" >/dev/null
cp "$KERNEL" "$WORK/kernel"
python3 "$HERE/bootimg.py" pack "$WORK" "$RAMDISK" "$OUT" >/dev/null
echo "  -> $OUT ($(stat -c %s "$OUT") bytes)"
