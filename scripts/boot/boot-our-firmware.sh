#!/bin/bash
# boot-our-firmware.sh -- get the device back onto our kernel and the
# firmware.
#
# WHY THIS EXISTS
# ---------------
#
# The camera's imaging server can crash the kernel (it drives the CSID
# and the ISP-IF directly, and one of the paths it can take -- giving
# the ISP-IF a single virtual channel that is not channel 0 -- is a
# NULL dereference in our driver).  When that happens the device
# reboots into the ROM's Android, and if it panics again before USB
# comes up it can end up not enumerating at all, which needs a
# physical power-cycle.
#
# So: when the device is unreachable, power-cycle it (hold the power
# key for ten seconds), and then run this script.  It puts the device
# into the bootloader and boots our image.
#
# Usage:
#   boot-our-firmware.sh [image]
#
# The default image is the one this session left on the device: our
# kernel (the vendor's device tree plus the volume keys' master
# enable) with the init stub's ramdisk, so the firmware starts on its
# own.
set -e

IMAGE=${1:-/tmp/opencode/boot-firmware.img}

[ -f "$IMAGE" ] || { echo "no image at $IMAGE" >&2; exit 1; }

echo "== the device"
timeout 20 adb get-state 2>&1 | head -1 || true

echo "== into the bootloader"
timeout 60 adb reboot bootloader 2>&1 | head -1 || true
for i in $(seq 1 40); do
	sleep 2
	if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
		echo "   in fastboot after ${i} tries"
		break
	fi
done

if ! timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
	echo "!! the device is not in fastboot: it needs a power-cycle" >&2
	echo "   (hold the power key for ten seconds, then run this again)" >&2
	exit 1
fi

echo "== booting $IMAGE"
timeout 180 fastboot boot "$IMAGE" 2>&1 | tail -2

echo "== waiting for adb"
for i in $(seq 1 30); do
	sleep 5
	if [ "$(timeout 8 adb get-state 2>/dev/null | tr -d '\r')" = "device" ]; then
		echo "   adb is back"
		break
	fi
done
timeout 20 adb shell 'cat /proc/version | head -1; ps -A -o NAME | grep -i micro' 2>&1 | sed 's/^/   /'
